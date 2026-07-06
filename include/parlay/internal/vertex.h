#ifndef PARLAY_VERTEX_H
#define PARLAY_VERTEX_H

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "depa.h"

// Graph-tracing Vertex: accumulates per-subtree work and records the
// fork-join DAG structure to a trace file for offline visualization.
//
// Each vertex carries a deterministic depa tid (see depa.h). One trace record
// is emitted per join, at join time -- when the full subtree cost is known --
// and only if the subtree did at least PARLAY_TRACE_CUTOFF_US (default 500)
// microseconds of work. Cheaper subtrees collapse into opaque leaves: their
// tids still advance (so ancestors stay consistent), but nothing is written.
//
// Trace file (PARLAY_TRACE_FILE, default "parlay.trace"), one record per line:
//   J <p.path> <p.depth> <j.path> <j.depth> <fork_ts> <join_ts> \
//     <p.work> <l.forks> <l.work> <r.forks> <r.work>
//   A <round#> <ts>                      (each parlay::augment call begins)
//   R <threads> <forks> <work> <ts>      (each parlay::augment call ends)
// where p is the parent's tid at the fork, j is the tid of the join vertex,
// paths are hex, and all times are integer nanoseconds. fork_ts/join_ts (and
// the A/R <ts>) are wall-clock timestamps relative to a common steady-clock
// epoch, for replaying the execution. p.work is the parent's accumulated
// work at the fork (the parent is stopped between fork and join, so at join
// time this->work still holds that value).

namespace parlay {
namespace internal {
namespace vertex {
using msize_t = unsigned long long int;
using ts_t = std::chrono::time_point<std::chrono::high_resolution_clock>;

inline msize_t now_ns() {
  static const auto epoch = std::chrono::steady_clock::now();
  return static_cast<msize_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - epoch).count());
}

inline msize_t trace_cutoff_ns() {
  static msize_t cutoff = []() -> msize_t {
    const char* s = std::getenv("PARLAY_TRACE_CUTOFF_US");
    msize_t us = 500;
    if (s != nullptr) us = std::strtoull(s, nullptr, 10);
    return us * 1000;
  }();
  return cutoff;
}

inline std::shared_ptr<spdlog::logger>& trace_logger() {
  static std::shared_ptr<spdlog::logger> logger = []() {
    const char* s = std::getenv("PARLAY_TRACE_FILE");
    std::string file = (s != nullptr) ? s : "parlay.trace";
    auto l = spdlog::basic_logger_mt("parlay_trace", file, /*truncate=*/true);
    l->set_pattern("%v");
    return l;
  }();
  return logger;
}

class Vertex {
 public:
  static thread_local Vertex* current;
  depa::tid_t tid;
  msize_t no_forks;
  msize_t work;
  msize_t fork_ts;  // now_ns() at this vertex's most recent fork
  ts_t curr_ts;

  Vertex() : tid(depa::init), no_forks(0), work(0), fork_ts(0) {}

  Vertex(const Vertex& other) : tid(other.tid), no_forks(other.no_forks), work(other.work), fork_ts(other.fork_ts), curr_ts(other.curr_ts) {}

  Vertex& operator=(const Vertex& other) {
    if (this != &other) {
      tid = other.tid;
      no_forks = other.no_forks;
      work = other.work;
      fork_ts = other.fork_ts;
      curr_ts = other.curr_ts;
    }
    return *this;
  }

  Vertex(Vertex&& other) noexcept :
      tid(other.tid), no_forks(other.no_forks), work(other.work), fork_ts(other.fork_ts), curr_ts(other.curr_ts) {}

  Vertex& operator=(Vertex&& other) noexcept {
    if (this != &other) {
      tid = other.tid;
      no_forks = other.no_forks;
      work = other.work;
      fork_ts = other.fork_ts;
      curr_ts = other.curr_ts;
    }
    return *this;
  }

  bool operator==(const Vertex& other) const {
    return (work == other.work && curr_ts == other.curr_ts);
  }

  void start() {
    this->curr_ts = std::chrono::high_resolution_clock::now();
  }

  void stop() {
    vertex::ts_t stop_ts = std::chrono::high_resolution_clock::now();
    vertex::msize_t elapsed_wc =
        (std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - this->curr_ts)).count();
    this->work += elapsed_wc;
  }

  void fork(vertex::Vertex* left_v, vertex::Vertex* right_v) {
    this->no_forks++;
    this->fork_ts = now_ns();
    if (depa::is_bogus(this->tid) || depa::tree_depth(this->tid) >= depa::MAX_FORK_DEPTH) {
      // Too deep to label; leave this subtree untraced.
      left_v->tid = depa::bogus;
      right_v->tid = depa::bogus;
    } else {
      auto children = depa::fork_tids(this->tid);
      left_v->tid = children.first;
      right_v->tid = children.second;
    }
  }

  void join(vertex::Vertex* left_v, vertex::Vertex* right_v, vertex::Vertex* join_v) {
    join_v->no_forks = this->no_forks + left_v->no_forks + right_v->no_forks;
    join_v->work = this->work + left_v->work + right_v->work;

    if (depa::is_bogus(left_v->tid) || depa::is_bogus(right_v->tid)) {
      join_v->tid = this->tid;
      return;
    }

    depa::tid_t j = depa::join_tids(left_v->tid, right_v->tid);
    join_v->tid = j;

    if (left_v->work + right_v->work >= trace_cutoff_ns()) {
      trace_logger()->info("J {:x} {} {:x} {} {} {} {} {} {} {} {}",
        this->tid.path, this->tid.depth, j.path, j.depth,
        this->fork_ts, now_ns(),
        this->work,
        left_v->no_forks, left_v->work,
        right_v->no_forks, right_v->work);
    }
  }

  static void trace_round_begin() {
    static std::atomic<msize_t> round{0};
    trace_logger()->info("A {} {}", round.fetch_add(1), now_ns());
  }

  static void trace_round_end(unsigned int num_threads, msize_t forks, msize_t work) {
    trace_logger()->info("R {} {} {} {}", num_threads, forks, work, now_ns());
    trace_logger()->flush();
  }

  void log(std::shared_ptr<spdlog::logger> logger_vertex, unsigned int num_threads) {
    logger_vertex->info("Threads:{},Forks:{},Work:{}", num_threads, this->no_forks, this->work);
  }
};
inline thread_local Vertex* Vertex::current = nullptr;
}  // namespace vertex
}  // namespace internal
}  // namespace parlay

#endif
