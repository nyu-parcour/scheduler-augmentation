# Deterministic mode: findings and proposed changes

This directory holds the plan for an opt-in deterministic mode of the parlay
scheduler, plus small programs that demonstrate each source of
non-determinism. This change adds no scheduler code; the changes below are
proposals to be implemented one by one.

Line numbers refer to commit `2a9ceba`.

## What "deterministic" means here

With the flag on, two runs of the same program should have:

1. **The same fork tree.** The same `par_do` calls happen at the same places,
   with the same split points.
2. **At most one root-to-leaf path per worker.** A worker waiting at a join
   never runs unrelated work on top of its waiting frame.

The flag does **not** promise the same task-to-worker assignment. Work
stealing decides that from timing (see C below), and fixing it would need a
static schedule or P = 1.

## The flag

Read from the environment once, when the scheduler is constructed, following
the `PARLAY_NUM_THREADS` pattern (`parallel.h:177-183`):

| Variable | Meaning |
|---|---|
| `PARLAY_DETERMINISTIC=1` | Turns deterministic mode on. Off when unset. |
| `PARLAY_DETERMINISTIC_GRANULARITY=<n>` | Leaf size used for `parallel_for` calls that pass granularity 0. Doesn't depend on P. |

A program's `--deterministic true|false` command-line option sets
`PARLAY_DETERMINISTIC` before it first calls parlay.

Two places construct a scheduler: the lazy thread-local one in
`get_current_scheduler` (`parallel.h:192-199`) and `execute_with_scheduler`
(`parallel.h:274-277`). Reading the variables in the `scheduler` constructor
(`scheduler.h:116-133`) and storing them as members, next to `num_threads`,
covers both.

## Findings

### A. The fork tree changes between runs

**A1. Timing-based granularity.** `parfor` (`scheduler.h:398-406`) calls
`get_granularity` (`scheduler.h:542-558`) when granularity is 0. That runs
iterations serially, doubling the batch, until one batch takes 1µs. The
number run serially (`done`) depends on timing and decides three things:

- whether the loop forks at all;
- the new `start` (line 403), which moves every split point `mid` in `parfor_`
  (`scheduler.h:567`);
- the leaf size, `max(done, n/(128·P))` (line 402).

This is the only clock read in parlaylib that affects scheduling. The other
clocks are in `internal/get_time.h` (a user-facing timer) and
`vertices/work_span_vertex.h` (measures time on purpose).

*Measured:* `parallel_for(0, 100000)` with a trivial body, 20 runs at P=8,
gave 15, 36, 64 or 149 forks. The counts vary at P=1 too.

**A2. Non-trivial element types skip the fixed threshold.**
`copy_granularity` and `initialization_granularity`
(`internal/sequence_base.h:60-66`) return `1 + 8192/sizeof(T)` only when `T`
is trivially copyable or trivially default-constructible. Otherwise they
return 0 and the loop takes the timing path in A1. `std::pair` is neither,
because it defines its own `operator=` and its default constructor zeroes the
members. Measured with Apple clang 21 / libc++: `std::pair<uint32_t,float>`
gives `trivially_copyable=0, trivially_default=0`.

*Measured:* a 288-element `sequence<std::pair<uint32_t,float>>` forked 0 times
in 200 runs, because its iterations are too cheap to reach 1µs. The fork seen
in `ann` hasn't been reproduced. It may come from slower iterations (page
faults on first touch, preemption, an expensive constructor) or from a type
other than `std::pair`. This needs the `ann` code (`space_alloc.h`) to settle.

**A3. Calls that always use the timing path, whatever the type.**
- `sequence::from_function` and everything built on it (`tabulate`, `map`,
  `delayed` materialization): `sequence.h:524-541`, default granularity 0.
- `destroy_all`: `internal/sequence_base.h:192`.
- `blocked_for`: `parallel.h:74`.
- Most of the 69 `parallel_for`/`blocked_for` calls in the library headers.

*Measured:* `tabulate` over 20,000 `uint64_t` gave 1 or 4 forks across 20
runs, although `uint64_t` is trivial.

All of these go through `parfor` with granularity 0, so fixing A1 inside
`parfor` covers every one.

### B. A waiting worker stacks unrelated work

**B1.** When the right half of a `par_do` was stolen, `pardo_impl`
(`scheduler.h:527-529`) calls `wait_until`, which calls `do_work_until`
(`scheduler.h:240-260`) unless `conservative` is set. The waiting worker
steals any task from any victim and runs it on top of its waiting frame.
That frame's memory stays allocated underneath, so one worker can hold
several root-to-leaf paths at once.

- The vertex slot is saved and restored correctly (`scheduler.h:242-258`),
  so per-strand accounting stays attributed to the right strand. The problem
  is physical stacking, not wrong bookkeeping.
- `par_do` and `parallel_for` already take a `conservative` argument that
  busy-waits instead (`scheduler.h:159-161`). Almost every library call site
  passes the default `false`; only `internal/sequence_ops.h:175` passes it
  through, via `fl_conservative`. A global mode therefore has to force it
  inside `wait_until`.

*Measured:* in a 15-level binary `par_do` tree at P=8, one thread held up to
17 frames beyond its own root-to-leaf path. Five runs gave 22, 5,542, 6,738,
9,189 and 11,839 such events. With `conservative=true`: 0 events in all 5
runs.

### C. Which worker runs which task (not addressed)

These decide who runs what. They are part of work stealing and are left as
they are:

| Location | What happens |
|---|---|
| `scheduler.h:297-306` | The steal victim is `hash(id) + hash(attempts[id])`. `attempts` counts for the whole program, so which victim is tried at a given moment depends on earlier timing. Whether a steal succeeds depends on timing too. |
| `scheduler.h:287-292` | Back-off after failed steals uses `sleep_for`. |
| `scheduler.h:127-132` | Workers start stealing as soon as their thread starts, so OS thread startup decides what they take first. |
| `scheduler.h:38-50, 308-350` | Elastic parallelism makes timeout and sleep decisions based on time. It is compiled off (`false` at line 39, although the comment at line 37 says the default is true). |

### D. Fixed for a given P, but changes with P

- The `parfor` leaf size term `n/(128·P)` (`scheduler.h:402`).
- `internal/quicksort.h:231`, `internal/collect_reduce.h:53` and
  `internal/counting_sort.h:125` pick block counts from `num_workers()`.
- P comes from `PARLAY_NUM_THREADS`, or `hardware_concurrency()` if that is
  unset (`parallel.h:177-183`).

*Measured:* not shown. In the `parallel_for` test the timing noise from A1
drowned out the P term.

### E. Outside the scheduler (not addressed)

- **Allocator.** `block_allocator` keeps one free list per OS thread
  (`internal/block_allocator.h:58`), and `pool_allocator` shares its
  large-size stacks between threads. The addresses returned, and when the
  allocator calls `::operator new`, depend on the schedule. That changes real
  memory use (RSS) but not `space_vertex`, which counts requested bytes. This
  repo has no allocator hook into the vertex, so how `ann`'s `space_alloc.h`
  counts is unchecked.
- **Checked and fine:** `hash_table.h` is the history-independent
  Shun–Blelloch table, so its result doesn't depend on insertion order.
  `random_generator` uses a fixed seed of 0. `write_min` on indices
  (`primitives.h:518-522`) gives a deterministic value. The deque is
  deterministic.

## Proposed changes

Each one is meant to be a separate commit.

### 1. Read the flag

- In the `scheduler` constructor, read `PARLAY_DETERMINISTIC` and
  `PARLAY_DETERMINISTIC_GRANULARITY` into two `const` members (for example
  `deterministic` and `deterministic_granularity`) and add public accessors.
- Reject a granularity of 0 or a non-number with a clear error.

### 2. Busy-wait at joins (fixes B1)

- `wait_until` (`scheduler.h:156-168`): take the busy-wait branch when
  `conservative || deterministic`.
- Check: `stacking` reports 0 events in deterministic mode without passing
  `conservative`.
- Cost: a worker whose right half was stolen sits idle until the thief
  finishes. Expect lower throughput.

### 3. Fixed, P-independent granularity (fixes A1, and A3 through it)

- `parfor` (`scheduler.h:398-406`): when `granularity == 0` and the flag is
  on, set `granularity = deterministic_granularity` and skip
  `get_granularity` and the `n/(128·P)` term entirely.
- Explicit nonzero granularities are left alone. They are already
  deterministic and P-independent (for example `copy_granularity` for
  trivial types, and the granularity of 1 in `sequence_ops.h:175`).
- Check: `fork_count pfor_default 100000` and `fork_count u64_tabulate 20000`
  each give a single value across runs, and the same value at
  P = 1, 2, 4 and 8.

### 4. Guard elastic parallelism

- If the flag is on while `PARLAY_ELASTIC_PARALLELISM` is compiled in, fail
  at scheduler construction, since its sleep and timeout decisions are timing
  based.
- Fix the "Default: true" comment at `scheduler.h:37`.

## Open questions

1. **Default granularity.** What should apply when `PARLAY_DETERMINISTIC=1`
   is set but `PARLAY_DETERMINISTIC_GRANULARITY` is not: a default (1? 1024?)
   or an error?
2. **Explicit granularities.** Should the override also replace explicit
   nonzero granularities, so one number controls the whole fork tree?
3. **Group D.** Should `quicksort`, `collect_reduce` and `counting_sort` stop
   depending on `num_workers()` in deterministic mode, so the fork tree is the
   same at every P?
4. **Group E.** Is the allocator in scope?
5. **A2.** Where is `space_alloc.h`, and what is the element type of the 288-
   element sequence that forked?

## Running the probes

```bash
determinism/run_tests.sh            # P=8, 20 runs per case
P=4 RUNS=50 determinism/run_tests.sh
```

| Program | What it does |
|---|---|
| `fork_count.cpp` | Counts forks in the DAG with a vertex, for sequence construction, `tabulate` and `parallel_for`. |
| `stacking.cpp` | Counts moments when a thread holds more active frames than its depth in the tree allows, which only happens through stacking. |
| `pair_traits.cpp` | Prints the type traits that decide whether `std::pair` gets the fixed threshold. |

The script prints the distinct results across runs as `value×count`. A single
entry means the result was stable.
