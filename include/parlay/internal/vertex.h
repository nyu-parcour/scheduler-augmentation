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
  msize_t no_forks;
  msize_t work;
  msize_t span;
  ts_t curr_ts;

  Vertex() : no_forks(0), work(0), span(0) {}

  Vertex(Vertex& other) : no_forks(other.no_forks), work(other.work), span(other.span), curr_ts(other.curr_ts) {}

  Vertex& operator=(const Vertex& other) {
    if (this != &other) {
      no_forks = other.no_forks;
      work = other.work;
      span = other.span;
      curr_ts = other.curr_ts;
    }
    return *this;
  }

  Vertex(Vertex&& other) noexcept :
      no_forks(other.no_forks), work(other.work), span(other.span), curr_ts(other.curr_ts) {}

  Vertex& operator=(Vertex&& other) noexcept {
    if (this != &other) {
      no_forks = other.no_forks;
      work = other.work;
      span = other.span;
      curr_ts = other.curr_ts;
    }
    return *this;
  }

  bool operator==(const Vertex& other) const {
    return (no_forks == other.no_forks && work == other.work && span == other.span && curr_ts == other.curr_ts);
  }

  void start() {
    // this->no_forks = 0;
    // this->work = 0;
    // this->span = 0;
    this->curr_ts = std::chrono::high_resolution_clock::now();
  }

  void stop() {
    vertex::ts_t stop_ts = std::chrono::high_resolution_clock::now();
    vertex::msize_t elapsed_wc =
        (std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - this->curr_ts)).count();
    this->work += elapsed_wc;
    this->span += elapsed_wc;
  }

  void fork(vertex::Vertex* left_v, vertex::Vertex* right_v) {
    this->no_forks++;
    // *left_v = *this;
    // *right_v = *this;
  }

  void join(vertex::Vertex* left_v, vertex::Vertex* right_v, vertex::Vertex* join_v) {
    join_v->no_forks = this->no_forks + left_v->no_forks + right_v->no_forks;
    join_v->work = this->work + left_v->work + right_v->work;
    if (left_v->span >= right_v->span) {
      join_v->span = this->span + left_v->span;
    } else {
      join_v->span = this->span + right_v->span;
    }
  }

  void log(std::shared_ptr<spdlog::logger> logger_vertex, unsigned int num_threads) {
    logger_vertex->info("Threads:{},Forks:{},Work:{},Span:{}", num_threads, this->no_forks, this->work, this->span);
  }
};
inline thread_local Vertex* Vertex::current = nullptr;
}  // namespace vertex
}  // namespace internal
}  // namespace parlay

#endif