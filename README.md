
# Scheduler Augmentation - Granularity Analysis

This branch presents a granularity analysis technique as described in
the following paper.

> *Scheduler Augmentation: A Lightweight, Customizable, Low-Cost Profiling Technique for Fork-Join Parallel Programs.*
> Sam Westrick, Darshan Dinesh Kumar, and Seong-Heon Jung.
> SPAA 2026

A few key files:
* [`include/parlay/scheduler.h`](include/parlay/scheduler.h) — the augmented scheduler (augmentation enabled by compiling with `-DPARLAY_AUG`)
* [`include/parlay/internal/vertex.h`](include/parlay/internal/vertex.h) — the `Vertex` class
* [`pbbs/benchmarks/rangeQuery2d/`](pbbs/benchmarks/rangeQuery2d/) - the
implementation of the range query benchmark studied here. We compare
`parallelPlaneSweep` (the original PBBS implementation) against our
`parallelPlaneSweepGrainFix` (with a modifed PAM GC to tune granularity).


Our implementation is a fork of
[ParlayLib](https://cmuparlay.github.io/parlaylib/). Other scheduler
augmentation experiments from the paper can be found on
the other branches ([`master`](https://github.com/nyu-parcour/scheduler-augmentation/tree/master) and [`space-profiling`](https://github.com/nyu-parcour/scheduler-augmentation/tree/space-profiling)).

## Reproducing the granularity analysis experiments

```
./reproduce.sh
```

This builds the benchmarks, generates input data, runs both versions
single-threaded to collect granularity logs, and produces the plots.

## Vertex definition

`Vertex` ([`include/parlay/internal/vertex.h`](include/parlay/internal/vertex.h))
represents a node in the parallel DAG. Each vertex accumulates:
* `work` — wall-clock time (nanoseconds) of serial work executed at this node
* `no_forks` — number of fork operations spawned beneath this node
* `phase` — an integer tag set by the application to distinguish execution phases

At each join, the vertex accumulates work and forks from both children. If the
total work exceeds 5000 ns, it logs a line to record the work-per-fork
ratio for the subdag associated with that join-point:

```
<phase> <work_ns> <forks> <work_ns/forks>
```

## Range query benchmark

There are two versions of the 2D range query benchmark under
[`pbbs/benchmarks/rangeQuery2d/`](pbbs/benchmarks/rangeQuery2d/):

**`parallelPlaneSweep`** (called `rq2d` in the paper)
— uses the stock [PAM](https://github.com/cmuparlay/PAM) library.

**`parallelPlaneSweepGrainFix`** (called `rq2d_ours` in the paper) —
uses a lightly modified PAM (`pam-gc-opt`,
branch `parsweep_exp` of `github.com/nyu-parcour/PAM`) whose GC
parallelization heuristic has been tuned to avoid generating computations that
are too fine-grained.

Both versions instrument three phases via `get_current_vertex()->phase`:
1. Build (plane sweep construction)
2. Query (batch range counting)
3. Clear (PAM GC)
