#ifndef PARLAY_VERTEX_H
#define PARLAY_VERTEX_H

#include <chrono>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace parlay {
namespace internal {
namespace vertex {
using msize_t = unsigned long long int;
using ts_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
class Vertex {
 public:
  static thread_local Vertex* current;
  int phase;
  msize_t no_forks;
  msize_t work;
  ts_t curr_ts;

  Vertex() : phase(0), no_forks(0), work(0) {}

  Vertex(const Vertex& other) :
    phase(other.phase),
    no_forks(other.no_forks),
    work(other.work),
    curr_ts(other.curr_ts)
    {}

  Vertex& operator=(const Vertex& other) {
    if (this != &other) {
      phase = other.phase;
      no_forks = other.no_forks;
      work = other.work;
      curr_ts = other.curr_ts;
    }
    return *this;
  }

  Vertex(Vertex&& other) noexcept :
      phase(other.phase),
      no_forks(other.no_forks),
       work(other.work),
       curr_ts(other.curr_ts)
      {}

  Vertex& operator=(Vertex&& other) noexcept {
    if (this != &other) {
      phase = other.phase;
      no_forks = other.no_forks;
      work = other.work;
      curr_ts = other.curr_ts;
    }
    return *this;
  }

  bool operator==(const Vertex& other) const {
    return (phase == other.phase && no_forks == other.no_forks && work == other.work && curr_ts == other.curr_ts);
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
    no_forks++;
    left_v->phase = phase;
    right_v->phase = phase;
  }

  void join(vertex::Vertex* left_v, vertex::Vertex* right_v, vertex::Vertex* join_v) {
    join_v->no_forks = this->no_forks + left_v->no_forks + right_v->no_forks;
    join_v->work = this->work + left_v->work + right_v->work;
    join_v->phase = phase;
    if (join_v->work > 5000) {
      auto w = join_v->work;
      auto f = join_v->no_forks;
      spdlog::info("{} {} {} {}", join_v->phase, w, f, w/f);
    }
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
