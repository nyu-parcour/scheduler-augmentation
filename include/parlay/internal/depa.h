#ifndef PARLAY_DEPA_H_
#define PARLAY_DEPA_H_

#include <cassert>
#include <cstdint>
#include <algorithm>
#include <utility>

// depa: deterministic DAG-address labels for fork-join computations.
//
// Every vertex in the dynamic fork-join graph gets a compact identifier
// (tid_t) computed purely from the fork/join structure -- no atomics, no
// global counters, reproducible across runs. A tid packs:
//   - path:  a bit-trail of left/right choices from the root, one bit per
//            level of live fork nesting, plus a sentinel bit just above the
//            current level.
//   - depth: two depths bit-packed into one word:
//       low TREE_DEPTH_BITS bits = tree depth (current live fork nesting)
//       remaining high bits      = dag depth (longest fork/join chain from
//                                  the root; grows at both forks and joins)

namespace parlay {
namespace depa {

constexpr uint32_t TREE_DEPTH_BITS = 6;
constexpr uint32_t TREE_DEPTH_MASK = (1u << TREE_DEPTH_BITS) - 1;

// path is 64 bits; tree depth h uses path bits [0, h] plus sentinel bit h+1,
// so the deepest representable nesting is 62.
constexpr uint32_t MAX_FORK_DEPTH = 62;

class tid_t {
public:
  uint64_t path;
  uint32_t depth;

  constexpr tid_t(uint64_t p, uint32_t d) : path(p), depth(d) {}
  constexpr tid_t() : path(0), depth(0) {}  // bogus default

  bool operator==(const tid_t& other) const {
    return path == other.path && depth == other.depth;
  }
};

constexpr tid_t init(1, 0);
constexpr tid_t bogus(0, 0);

inline bool is_bogus(tid_t tid) {
  return tid.path == 0;
}

inline uint32_t tree_depth(tid_t tid) {
  return tid.depth & TREE_DEPTH_MASK;
}

inline uint32_t dag_depth(tid_t tid) {
  return tid.depth >> TREE_DEPTH_BITS;
}

inline uint64_t norm_path(tid_t tid) {
  uint32_t td = tree_depth(tid);
  return tid.path & ((uint64_t(1) << (td + 1)) - 1);
}

inline std::pair<tid_t, tid_t> fork_tids(tid_t tid) {
  uint32_t h = tree_depth(tid);
  assert(!is_bogus(tid));
  assert(h < MAX_FORK_DEPTH);

  tid_t t1(
    (tid.path & ~(uint64_t(1) << h)) | (uint64_t(1) << (h + 1)),
    tid.depth + (1u << TREE_DEPTH_BITS) + 1
  );

  tid_t t2(
    (tid.path | (uint64_t(1) << h)) | (uint64_t(1) << (h + 1)),
    tid.depth + (1u << TREE_DEPTH_BITS) + 1
  );

  assert(tree_depth(t1) == tree_depth(tid) + 1);
  assert(tree_depth(t2) == tree_depth(tid) + 1);
  assert(dag_depth(t1) == dag_depth(tid) + 1);
  assert(dag_depth(t2) == dag_depth(tid) + 1);
  assert((norm_path(t1) ^ norm_path(t2)) == (uint64_t(1) << h));

  return std::make_pair(t1, t2);
}

inline tid_t join_tids(tid_t t1, tid_t t2) {
  assert(!is_bogus(t1) && !is_bogus(t2));
  assert(tree_depth(t1) == tree_depth(t2));
  assert(tree_depth(t1) >= 1);

  uint32_t td = tree_depth(t1) - 1;
  uint32_t dd = std::max(dag_depth(t1), dag_depth(t2)) + 1;

  tid_t tid(
    t1.path | (uint64_t(1) << td),
    (dd << TREE_DEPTH_BITS) + td
  );

  assert(tree_depth(tid) == tree_depth(t1) - 1);

  return tid;
}

}  // namespace depa
}  // namespace parlay

#endif  // PARLAY_DEPA_H_
