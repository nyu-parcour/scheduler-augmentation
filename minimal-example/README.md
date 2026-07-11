# Minimal example: a custom vertex for work and burdened-span analysis

This directory is a self-contained demonstration of *scheduler augmentation*
with a user-defined vertex. It instruments parallel fib with a custom
`burdened_vertex` that measures the **work** and **burdened span** of the
computation and reports the **estimated parallelism** (their ratio).

```
minimal-example/
├── burdened_vertex.h   the custom vertex definition
├── main.cpp            parallel fib (grain cutoff at n = 20) + reporting
├── Makefile            standalone build (only needs ../include)
└── README.md
```

## Build and run

```
make
./fib              # fib(42), default 500us burden
./fib 38           # smaller n
./fib 38 100       # 100us burden per join
```

The number of worker threads is controlled by `PARLAY_NUM_THREADS`
(default: all hardware threads). Sample output:

```
$ PARLAY_NUM_THREADS=4 ./fib
fib(42) = 267914296  (threads: 4, burden: 500 us)

end-to-end time:           262.889 ms
work:                     1043.839 ms
burdened span:              12.036 ms
estimated parallelism:        86.7
```

Lowering the burden models a scheduler with cheaper fork-joins and
correspondingly raises the estimated parallelism:

```
$ PARLAY_NUM_THREADS=4 ./fib 42 100
...
burdened span:               2.320 ms
estimated parallelism:       414.6
```

## How it works

The scheduler is parameterized by a *vertex* type that observes the
fork-join structure of the computation (see `include/parlay/vertex.h` for
the full concept). A vertex provides four hooks:

| hook                        | called when                                        |
|-----------------------------|----------------------------------------------------|
| `start()` / `stop()`        | a strand resumes / pauses executing                |
| `fork(left, right)`         | the strand forks; `left`/`right` are fresh vertices for the two children |
| `join(left, right, join_v)` | the strand joins; fold the parent and finished children into the fresh vertex `join_v`, which the scheduler then installs as the continuing strand's vertex |

`burdened_vertex` accumulates wall-clock nanoseconds between `start()` and
`stop()` into both `work` and `span`, sums everything at joins, and — the
one twist over a plain work/span vertex — charges a fixed **burden** to the
span at every join:

```c++
join_v->span = span + max(left->span, right->span) + burden;
```

The burden (default 500us) models the scheduling cost of a fork-join on the
critical path, so `work / burdened_span` is a more honest estimate of the
parallelism a real work-stealing scheduler can exploit than the ratio with
the raw span. It is a single constant for the whole analysis
(`burdened_vertex::burden`, settable before the augmented region — here from
the command line).

The instrumented region is delimited with `parlay::augment`, which installs
the vertex for the dynamic extent of the function and returns the final
vertex:

```c++
auto v = parlay::augment(burdened_vertex{}, [&]() {
  result = fib(n);
});
// v.work, v.span are the totals for the whole region
```

No rebuild of anything is needed to use a new vertex type: under the default
scheduler configuration (`parlay::dynamic_vertex`), `augment` accepts any
vertex type at runtime. The only requirement is that the type fits in the
type-erased handle (`sizeof <= 48` bytes by default; raise with
`-DPARLAY_DYNAMIC_VERTEX_CAPACITY=<bytes>`).

## Baseline builds

Compiling with `-DPARLAY_NOOP_VERTEX` removes all augmentation machinery
from the scheduler (the generated code is identical to stock parlay);
`augment` then just runs the function and returns the initial vertex
unchanged, so this example would report zero work and span. Compiling with
`-DPARLAY_VERTEX_TYPE=burdened_vertex` (with the header pre-included) bakes
the hooks in statically with no type erasure, for minimum measurement
overhead.
