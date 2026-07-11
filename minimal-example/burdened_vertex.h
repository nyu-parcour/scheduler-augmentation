// A custom vertex for work and burdened-span analysis.
//
// Work and span are measured in nanoseconds of wall-clock time, like
// parlay::work_span_vertex. In addition, every join point charges a fixed
// "burden" to the span, modeling the scheduling cost (spawn, steal, sync)
// of each fork-join on the critical path:
//
//   join_v->span = parent->span + max(left->span, right->span) + burden
//
// The resulting burdened span gives a more realistic lower bound on parallel
// running time than the raw span, and work / burdened-span estimates the
// parallelism actually exploitable by a work-stealing scheduler.
//
// The burden is a single constant for the whole analysis (default 500us);
// set burdened_vertex::burden before the augmented region to change it.

#ifndef BURDENED_VERTEX_H_
#define BURDENED_VERTEX_H_

#include <chrono>

class burdened_vertex {
 public:
  static constexpr bool enabled = true;

  using nanos_t = unsigned long long;
  using ts_t = std::chrono::time_point<std::chrono::high_resolution_clock>;

  // Span burden per join, in nanoseconds.
  static inline nanos_t burden = 500'000;  // 500us

  nanos_t work = 0;
  nanos_t span = 0;  // burdened span
  ts_t curr_ts{};

  void start() {
    curr_ts = std::chrono::high_resolution_clock::now();
  }

  void stop() {
    ts_t stop_ts = std::chrono::high_resolution_clock::now();
    nanos_t elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - curr_ts).count();
    work += elapsed;
    span += elapsed;
  }

  void fork(burdened_vertex*, burdened_vertex*) {}

  void join(burdened_vertex* left, burdened_vertex* right, burdened_vertex* join_v) {
    join_v->work = work + left->work + right->work;
    join_v->span = span
        + ((left->span >= right->span) ? left->span : right->span)
        + burden;
  }
};

#endif  // BURDENED_VERTEX_H_
