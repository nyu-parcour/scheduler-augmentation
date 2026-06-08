#ifndef PARLAY_VERTEX_H
#define PARLAY_VERTEX_H

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"


using isize_t = std::make_signed<std::size_t>::type;
namespace parlay {
namespace internal {
namespace vertex {
class Vertex {
  isize_t s1 = 0;
  isize_t sinf = 0;
  isize_t gross = 0;
 public:
  static thread_local Vertex* current;

  Vertex() : s1(0), sinf(0), gross(0) {}

  Vertex(Vertex& other) : s1(other.s1), sinf(other.sinf), gross(other.gross) {}

  Vertex& operator=(const Vertex& other) {
    if (this != &other) {
      s1 = other.s1;
      sinf = other.sinf;
      gross = other.gross;
    }
    return *this;
  }

  Vertex(Vertex&& other) noexcept : s1(other.s1), sinf(other.sinf), gross(other.gross) {}

  Vertex& operator=(Vertex&& other) noexcept {
    if (this != &other) {
      s1 = other.s1;
      sinf = other.sinf;
      gross = other.gross;
    }
    return *this;
  }

  bool operator==(const Vertex& other) const { return (s1 == other.s1 && sinf == other.sinf && gross == other.gross); }

  isize_t get_s1() const { return s1; }
  isize_t get_sinf() const { return sinf; }
  isize_t get_gross() const { return gross; }

  void start() {}

  void stop() {}

  void fork(vertex::Vertex* left, vertex::Vertex* right) {}

  void join(vertex::Vertex* left, vertex::Vertex* right, vertex::Vertex* cont) {
    cont->s1 = std::max(
                        std::max(this->s1, this->gross + left->s1),
                        this->gross + left->gross + right->s1);
    cont->sinf = std::max(this->sinf,
                          this->gross + left->sinf + right->sinf);
    cont->gross = this->gross + left->gross + right->gross;
  }

  void log(std::shared_ptr<spdlog::logger> logger_vertex, unsigned int num_threads) {
    logger_vertex->info("Threads:{},s1:{},sinf:{},gross:{}", num_threads, this->s1, this->sinf, this->gross);
  }

  void allocate(std::size_t diff) {
    auto diff2 = static_cast<isize_t>(diff);
    this->gross += diff2;
    this->s1 = std::max<isize_t>(this->s1, gross);
    this->sinf = std::max<isize_t>(this->sinf, gross);
  }

  void deallocate(std::size_t diff) {
    auto diff2 = static_cast<isize_t>(diff);
    this->gross -= diff2;
    this->s1 = std::max<isize_t>(this->s1, gross);
    this->sinf = std::max<isize_t>(this->sinf, gross);
  }
};
thread_local Vertex* Vertex::current = nullptr;
}  // namespace vertex
}  // namespace internal
}  // namespace parlay

#endif

