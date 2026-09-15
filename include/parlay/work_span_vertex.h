// work_span_vertex: measures work, span, and fork count of an augmented
// computation (see vertex.h for the vertex concept).
//
// Work and span are measured in nanoseconds of wall-clock time; forks are
// counted per pardo. Use with
//   auto v = parlay::augment(parlay::work_span_vertex{}, [&]() { ... });
//   v.log(parlay::num_workers());   // appends to logs_vertex.txt
//
// This header owns the spdlog dependency (vendored at
// <repo>/include/spdlog/include); the parlay core does not use it.

#ifndef PARLAY_WORK_SPAN_VERTEX_H_
#define PARLAY_WORK_SPAN_VERTEX_H_

#include <chrono>
#include <memory>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace parlay {

class work_span_vertex {
 public:
  static constexpr bool enabled = true;

  using msize_t = unsigned long long int;
  using ts_t = std::chrono::time_point<std::chrono::high_resolution_clock>;

  msize_t no_forks = 0;
  msize_t work = 0;
  msize_t span = 0;
  ts_t curr_ts{};

  void start() {
    curr_ts = std::chrono::high_resolution_clock::now();
  }

  void stop() {
    ts_t stop_ts = std::chrono::high_resolution_clock::now();
    msize_t elapsed_wc =
        (std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - curr_ts)).count();
    work += elapsed_wc;
    span += elapsed_wc;
  }

  void fork(work_span_vertex*, work_span_vertex*) {
    no_forks++;
  }

  void join(work_span_vertex* left, work_span_vertex* right, work_span_vertex* join_v) {
    join_v->no_forks = no_forks + left->no_forks + right->no_forks;
    join_v->work = work + left->work + right->work;
    join_v->span = span + ((left->span >= right->span) ? left->span : right->span);
  }

  void log(unsigned int num_threads) {
    // Lazy so the logger is registered once per process, on first use
    // (registering "vertex_logger" twice would throw).
    static std::shared_ptr<spdlog::logger> logger =
        spdlog::basic_logger_mt("vertex_logger", "logs_vertex.txt");
    logger->info("Threads:{},Forks:{},Work:{},Span:{}", num_threads, no_forks, work, span);
  }
};

}  // namespace parlay

#endif  // PARLAY_WORK_SPAN_VERTEX_H_
