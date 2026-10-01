// space_vertex: measures the space usage of an augmented computation
//
//
// The following quantities are tracked in bytes:
//
//   gross   the net allocation of the strand (allocations minus deallocations);
//           it combines additively at a join.
//   s1      the peak memory of a 1-processor (depth-first) execution: the left
//           subtree runs to completion while the parent's gross is live, then
//           the right subtree runs with the left's gross still live.
//   s1star  the peak memory of the *worst* 1-processor execution: like s1, but
//           at every fork the two subtrees may run in either order, and the
//           order that peaks higher is the one taken. This is the quantity
//           that bounds a P-processor execution (peak <= P * s1star), whereas
//           s1 only describes the one left-to-right schedule.
//   sinf    the peak memory of an infinite-processor execution, where both
//           subtrees are resident at the same time.
//   min_prefix
//           the lowest gross reached by any prefix of any 1-processor
//           execution; at most 0, and below 0 only if the strand frees memory
//           it did not allocate.
//
// s1 <= s1star <= sinf.
//
// The spine bound. A strand's history is a sequence of its own allocations
// and deallocations interleaved with the parallel blocks (fork ... join) it
// has completed; that sequence is its spine. Memory allocated on the spine
// before a block is shared by every processor working inside the block, so
// only the block's own peak is replicated P times:
//
//   spine_seq      S: the peak gross along the spine, counting each block's
//                  own peak as 0 (the block's net gross still carries forward).
//   spine_par      R1star_partial: the largest s1star of any block on the
//                  spine.
//   spine_par_min  the smallest min_prefix of any block on the spine.
//
// When spine_par_min == 0, i.e. no block frees memory that was live before it
// started, peak <= S + P * R1star_partial. This is splang's highwater_spine
// (Summary.spineSeq / spinePar / spineParMin). It is not always tighter than
// P * s1star, because S and R1star_partial may come from different points on
// the spine; take the smaller of the two.

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
  isize_t s1star = 0;
  isize_t sinf = 0;
  isize_t gross = 0;
  isize_t min_prefix = 0;
  isize_t spine_seq = 0;
  isize_t spine_par = 0;
  isize_t spine_par_min = 0;

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
    // The block just joined, measured on its own, from a gross of 0. Either
    // subtree may run first, and a prefix of the block is a prefix of each.
    const isize_t block_s1star =
        std::max(std::max(left->s1star, left->gross + right->s1star),
                 std::max(right->s1star, right->gross + left->s1star));
    const isize_t block_min_prefix =
        std::min<isize_t>(0, left->min_prefix + right->min_prefix);
    join_v->s1star = std::max(s1star, gross + block_s1star);
    join_v->min_prefix = std::min(min_prefix, gross + block_min_prefix);
    // Fully parallel: both subtrees resident on top of the parent's gross.
    join_v->sinf = std::max(sinf, gross + left->sinf + right->sinf);
    join_v->gross = gross + left->gross + right->gross;
    // The block extends this strand's spine. On the spine it contributes only
    // the level it started from; its peak goes to spine_par instead.
    join_v->spine_seq = std::max(spine_seq, gross);
    join_v->spine_par = std::max(spine_par, block_s1star);
    join_v->spine_par_min = std::min(spine_par_min, block_min_prefix);
  }

  // Reported by the allocator on every allocation made by this strand.
  void allocate(std::size_t n) {
    gross += static_cast<isize_t>(n);
    s1 = std::max(s1, gross);
    s1star = std::max(s1star, gross);
    sinf = std::max(sinf, gross);
    spine_seq = std::max(spine_seq, gross);
  }

  // Reported by the allocator on every deallocation made by this strand.
  void deallocate(std::size_t n) {
    // The level before the free. Right after a join it can exceed spine_seq,
    // since a block that leaves memory behind only records its starting level.
    spine_seq = std::max(spine_seq, gross);
    gross -= static_cast<isize_t>(n);
    s1 = std::max(s1, gross);
    s1star = std::max(s1star, gross);
    sinf = std::max(sinf, gross);
    min_prefix = std::min(min_prefix, gross);
  }

  void log(unsigned int num_threads) {
    static std::shared_ptr<spdlog::logger> logger =
        spdlog::basic_logger_mt("space_vertex_logger", "logs_vertex.txt");
    logger->info(
        "Threads:{},s1:{},s1star:{},sinf:{},gross:{},min_prefix:{},spine_seq:{},"
        "spine_par:{},spine_par_min:{}",
        num_threads, s1, s1star, sinf, gross, min_prefix, spine_seq, spine_par,
        spine_par_min);
  }
};

}  // namespace parlay

#endif  // PARLAY_SPACE_VERTEX_H_
