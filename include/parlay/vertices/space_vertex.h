// space_vertex: measures the space usage of an augmented computation
//
//
// Three quantities are tracked in bytes:
//
//   gross  the net allocation of the strand (allocations minus deallocations);
//          it combines additively at a join.
//   s1     the peak memory of a 1-processor (depth-first) execution: the left
//          subtree runs to completion while the parent's gross is live, then
//          the right subtree runs with the left's gross still live.
//   sinf   the peak memory of an infinite-processor execution, where both
//          subtrees are resident at the same time.

#ifndef PARLAY_SPACE_VERTEX_H_
#define PARLAY_SPACE_VERTEX_H_

#include <cstddef>

#include <algorithm>
#include <memory>
#include <type_traits>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace parlay {

class space_vertex {
 public:
  static constexpr bool enabled = true;

  // Signed, because a strand may free memory that was allocated by another
  // strand, leaving its own gross negative.
  using isize_t = std::make_signed_t<std::size_t>;

  isize_t s1 = 0;
  isize_t sinf = 0;
  isize_t gross = 0;

  // Space is accounted at the allocator, not on a clock, so there is nothing
  // to pause or resume on a strand boundary.
  void start() {}

  void stop() {}

  void fork(space_vertex*, space_vertex*) {}

  void join(space_vertex* left, space_vertex* right, space_vertex* join_v) {
    // Depth-first: the parent's peak, then the left subtree on top of the
    // parent's gross, then the right subtree on top of both.
    join_v->s1 = std::max(std::max(s1, gross + left->s1),
                          gross + left->gross + right->s1);
    // Fully parallel: both subtrees resident on top of the parent's gross.
    join_v->sinf = std::max(sinf, gross + left->sinf + right->sinf);
    join_v->gross = gross + left->gross + right->gross;
  }

  // Reported by the allocator on every allocation made by this strand.
  void allocate(std::size_t n) {
    gross += static_cast<isize_t>(n);
    s1 = std::max(s1, gross);
    sinf = std::max(sinf, gross);
  }

  // Reported by the allocator on every deallocation made by this strand.
  void deallocate(std::size_t n) {
    gross -= static_cast<isize_t>(n);
    s1 = std::max(s1, gross);
    sinf = std::max(sinf, gross);
  }

  void log(unsigned int num_threads) {
    static std::shared_ptr<spdlog::logger> logger =
        spdlog::basic_logger_mt("space_vertex_logger", "logs_vertex.txt");
    logger->info("Threads:{},s1:{},sinf:{},gross:{}", num_threads, s1, sinf, gross);
  }
};

}  // namespace parlay

#endif  // PARLAY_SPACE_VERTEX_H_
