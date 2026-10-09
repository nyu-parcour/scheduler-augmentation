# Deterministic mode: findings and changes

This directory holds the findings and design for an opt-in deterministic
mode of the parlay scheduler, plus small programs that demonstrate each
source of non-determinism. The four changes under [Changes](#changes) are
implemented on this branch, one commit each.

Line numbers in the findings refer to commit `2a9ceba`, before the changes.

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
| `PARLAY_DETERMINISTIC_GRANULARITY=<n>` | Leaf size used for `parallel_for` calls that pass granularity 0. Doesn't depend on P. Defaults to 1. |

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

*In `ann`* (branch `r1star`, `splang-bench/ann.cpp:54`): the element type is
`pid = std::pair<int, float>`, so every `pid` sequence in `beam_search` takes
the timing path at any length:

- `unvisited_frontier` and `new_frontier`: default construction;
- the `frontier` rebuild on every loop iteration: range construction;
- `sorted_candidates` and `visited_copy`: copy construction.

The comment in `include/parlay/vertices/space_alloc.h` on `r1star` says
`space_sequence` forks only above `1 + 8192/sizeof(T)` elements. That's only
true for trivial types, so it doesn't hold for `pid`. The fix belongs on
`r1star`, not here.

`visited.insert` is not a source. Parlay marks `std::pair` of trivial types as
trivially relocatable (`type_traits.h:346`), so `insert` moves elements with
`memcpy` in 1,024-element chunks with granularity 1. 288 elements never fork.

*Measured:* a 288-element `sequence<std::pair<uint32_t,float>>` forked 0 times
in 200 runs of the probe, because its iterations are too cheap to reach 1µs.
In `ann` those constructions run inside a per-query `parallel_for` with all
workers busy, which makes one slow early batch far more likely. That is the
likely explanation for the observed fork; it has not been reproduced.

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
| `scheduler.h:38-50, 308-350` | Elastic parallelism makes timeout and sleep decisions based on time. It is compiled off (`false` at line 39, although the comment at line 37 said the default is true). Change 4 fixes the comment and rejects deterministic mode when it is compiled in. |

### D. Fixed for a given P, but changes with P (deferred)

P comes from `PARLAY_NUM_THREADS`, or `hardware_concurrency()` if that is
unset (`parallel.h:177-183`).

| Place | How it depends on P | Reached from |
|---|---|---|
| `parfor` leaf size `n/(128·P)` (`scheduler.h:402`) | Leaf size | Every `parallel_for` with granularity 0. Removed in deterministic mode by change 3. |
| `internal/collect_reduce.h:53-54` (`collect_reduce_few`) | About 4·P blocks | `reduce_by_index`, `histogram_by_index`, `remove_duplicate_integers`, `group_by_index` (`internal/group_by.h:246-313`), when n ≥ 8,192 and there are few buckets |
| `internal/counting_sort.h:125,138` (`count_sort_`) | Only P = 1 vs P > 1: P = 1 runs serially | `counting_sort*`, `integer_sort`, `stable_integer_sort`, `random_shuffle`/`random_permutation` (`random.h:124`), and the hashed `reduce_by_key`, `group_by_key`, `histogram_by_key`, `remove_duplicates` (`internal/group_by.h:128-222`) |
| `internal/quicksort.h:231` (`p_quicksort_`) | Cutoff `3n/P` | Nothing. `p_quicksort_` has no callers, so it's dead code. `parlay::sort` uses sample sort, which doesn't read P. |

None of the `r1star` benchmarks reach the last three rows. `ann` uses
`tabulate` and `parallel_for`, `strassen` uses `tabulate`, and `nqueens` and
`allocfree` use only `par_do`. Deferred until a profiled benchmark uses one
of them.

*Measured:* not shown. In the `parallel_for` test the timing noise from A1
drowned out the P term.

### E. Outside the scheduler (out of scope)

- **Allocator.** `block_allocator` keeps one free list per OS thread
  (`internal/block_allocator.h:58`), and `pool_allocator` shares its
  large-size stacks between threads. The addresses returned, and when the
  allocator calls `::operator new`, depend on the schedule. That changes real
  memory use (RSS) only. `space_alloc.h` on `r1star` charges the requested
  bytes (`n * sizeof(T)`) to both the vertex and the live/high-water
  counters, so none of the profiling counters see allocator behavior.
  Decision: leave `parlay::allocator` alone for now and revisit if needed.
- **Checked and fine:** `hash_table.h` is the history-independent
  Shun–Blelloch table, so its result doesn't depend on insertion order.
  `random_generator` uses a fixed seed of 0. `write_min` on indices
  (`primitives.h:518-522`) gives a deterministic value. The deque is
  deterministic.

## Changes

Each one is a separate commit on this branch. The results are from
`run_tests.sh` at P=8 with 20 runs per case, unless noted otherwise.

### 1. Read the flag

- The `scheduler` constructor reads both variables into two `const` members,
  with public accessors `deterministic()` and `deterministic_granularity()`.
- `PARLAY_DETERMINISTIC`: unset or `0` is off, `1` is on, and any other value
  throws.
- `PARLAY_DETERMINISTIC_GRANULARITY` must be a positive integer and defaults
  to 1. It is read only when the flag is on, so it has no effect otherwise
  and a bad value is ignored.
- Bad values throw `std::invalid_argument` with a message naming the
  variable, before the scheduler takes over `worker_info` or starts any
  threads.
- Result: values like `yes`, `0`, `abc`, `-5`, `12x`, an empty string and an
  overflowing number are rejected with the message.

### 2. Busy-wait at joins (fixes B1)

- `wait_until` (`scheduler.h:156-168`): take the busy-wait branch when
  `conservative || deterministic`.
- Check: `stacking` reports 0 events in deterministic mode without passing
  `conservative`.
- Result: 0 events in every run with the flag on, against 1,300–16,000
  events with it off.
- Cost: a worker whose right half was stolen sits idle until the thief
  finishes. Expect lower throughput. The `stacking` probe took about 0.04s
  with the flag on against 0.03s with it off (3 runs each), which shows the
  slowdown is there but not how large it is.

### 3. Fixed, P-independent granularity (fixes A1, and A3 through it)

- `parfor` (`scheduler.h:398-406`): when `granularity == 0` and the flag is
  on, set `granularity = deterministic_granularity` and skip
  `get_granularity` and the `n/(128·P)` term entirely.
- Explicit nonzero granularities are left alone (decided). They are already
  deterministic and P-independent, for example `copy_granularity` for trivial
  types (1025 for 8-byte types), the granularity of 1 in `sequence_ops.h:175`,
  or 2000 in `internal/quicksort.h:236`.
- Granularity 0 includes the type-based ones that come out to 0 for
  non-trivial types. With the default of 1, every `pid` sequence that
  `ann`'s `beam_search` builds forks down to single elements; a 288-element
  construction becomes about 287 forks. That is deterministic, and those
  branches don't allocate, but it costs time and grows the fork tree well
  beyond a normal run. Setting `PARLAY_DETERMINISTIC_GRANULARITY=1025` for
  `ann` would match what trivial 8-byte types get.
- Check: `fork_count pfor_default 100000` and `fork_count u64_tabulate 20000`
  each give a single value across runs, and the same value at
  P = 1, 2, 4 and 8.
- Result: every fork count is the same in every run and at every P.

  | Case | Flag off | Flag on, granularity 1 | Flag on, granularity 1025 |
  |---|---|---|---|
  | `pfor_default 100000` | 15, 36, 64 or 149 | 99999 | 149 |
  | `u64_tabulate 20000` | mostly 4, sometimes 1 or 769 | 19999 | 27 |
  | `pair_fill 288` | 0, once 27 | 287 | 0 |
  | `pair_tabulate 20000` | 4, once 27 | 19999 | 27 |
  | `u64_fill 2000` (explicit 1025) | 2 | 2 | 2 |
  | `pfor_gran1 1000` (explicit 1) | 999 | 999 | 999 |

  The last two rows show that explicit granularities are unchanged.

### 4. Guard elastic parallelism

- If the flag is on while `PARLAY_ELASTIC_PARALLELISM` is compiled in,
  scheduler construction throws, since elastic parallelism makes its sleep
  and timeout decisions based on time.
- Fix the "Default: true" comment at `scheduler.h:37`.
- Result: a build with `-DPARLAY_ELASTIC_PARALLELISM=true` runs with the flag
  off and is rejected with it on.

## Decisions

| Question | Decision |
|---|---|
| How to turn it on | Environment variable `PARLAY_DETERMINISTIC=1` |
| Waiting at a join | Busy-wait (change 2) |
| Granularity when parlay would pick | `PARLAY_DETERMINISTIC_GRANULARITY`, independent of P, default 1 |
| Explicit nonzero granularities | Left unchanged |
| Group D (`num_workers()`-based sizing) | Deferred; no profiled benchmark reaches it |
| Group E (allocator) | Out of scope for now |

## Still open

- **A2 in `ann`.** The observed 288-element fork is explained by the type
  (`pid` takes the timing path). The probe has now reproduced it once:
  `pair_fill 288` forked 27 times in 1 of 20 flag-off runs, after 0 forks in
  the earlier 200. It hasn't been reproduced in `ann` itself. In
  deterministic mode it can't happen, because the timing path is gone.

## Running the probes

```bash
determinism/run_tests.sh            # P=8, 20 runs per case
P=4 RUNS=50 determinism/run_tests.sh
PARLAY_DETERMINISTIC_GRANULARITY=1025 determinism/run_tests.sh
```

The script first checks flag parsing and the elastic-parallelism guard, then
runs every probe twice: once with the flag unset and once with
`PARLAY_DETERMINISTIC=1`. The deterministic pass uses
`PARLAY_DETERMINISTIC_GRANULARITY` from the environment, 1 if unset.

| Program | What it does |
|---|---|
| `fork_count.cpp` | Counts forks in the DAG with a vertex, for sequence construction, `tabulate` and `parallel_for`. |
| `stacking.cpp` | Counts moments when a thread holds more active frames than its depth in the tree allows, which only happens through stacking. |
| `pair_traits.cpp` | Prints the type traits that decide whether `std::pair` gets the fixed threshold. |

The script prints the distinct results across runs as `value×count`. A single
entry means the result was stable.
