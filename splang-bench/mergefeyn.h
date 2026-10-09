// MergeFeyn, divide-and-conquer Feynman-path simulation with merging, with
// every state charged to the current strand's space_vertex.
//
// A port of ../mergefeyn/src/{state,gates,merge_feyn}.hpp. Two things differ
// from the original, neither of which changes what is allocated or where:
//
//   - States are space_sequences rather than parlay::sequences, and the
//     parlay primitives that allocate (pack_index, tabulate, filter) are
//     written out so that their buffers are charged too. They allocate the
//     same buffers parlay's do: pack's per-block counts, its output, and the
//     tabulated result.
//   - It is C++17 like the rest of splang-bench, so spans are pointer and
//     length pairs and std::ranges calls are their std:: counterparts.
//
// Only the gates the W-state circuit uses are ported: single-qubit unitaries
// (x, ry), cx and cz.

#ifndef SPLANG_BENCH_MERGEFEYN_H_
#define SPLANG_BENCH_MERGEFEYN_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <parlay/parallel.h>
#include <parlay/vertices/space_alloc.h>

namespace mergefeyn {

template <typename T>
using seq = parlay::space_sequence<T>;

// Bit q of a basis index is the value of qubit q (so at most 64 qubits).
using BasisIdx = std::uint64_t;

struct Complex {
  double re = 0;
  double im = 0;

  // Weights below this threshold (in both components) are treated as zero,
  // and the path that carries them is dropped.
  static constexpr double zero_threshold = 1e-8;

  bool is_zero() const { return std::abs(re) < zero_threshold && std::abs(im) < zero_threshold; }

  friend Complex operator+(Complex a, Complex b) { return {a.re + b.re, a.im + b.im}; }
  friend Complex operator*(Complex a, Complex b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
  }
};

struct WeightedIdx {
  BasisIdx idx;
  Complex weight;
};

// parlay::internal::pack with charged buffers: count the kept elements of
// each 1024-element block in parallel, scan the counts, and write each
// block's elements at its offset. f(i) is element i and keep(i) whether it is
// kept. One block packs serially.
template <typename T, typename F, typename Keep>
seq<T> pack(std::size_t n, F f, Keep keep) {
  constexpr std::size_t block = 1024;  // parlay::internal::_block_size
  const std::size_t l = (n + block - 1) / block;
  if (l <= 1) {
    std::size_t m = 0;
    for (std::size_t i = 0; i < n; i++) m += keep(i);
    auto out = seq<T>::uninitialized(m);
    for (std::size_t i = 0, k = 0; i < n; i++)
      if (keep(i)) out[k++] = f(i);
    return out;
  }
  auto sums = seq<std::size_t>::uninitialized(l);
  parlay::parallel_for(0, l, [&](std::size_t b) {
    std::size_t c = 0;
    for (std::size_t i = b * block; i < std::min(n, (b + 1) * block); i++) c += keep(i);
    sums[b] = c;
  }, 1);
  // parlay scans these serially too unless there are more than 2048 blocks.
  std::size_t m = 0;
  for (std::size_t b = 0; b < l; b++) m += std::exchange(sums[b], m);
  auto out = seq<T>::uninitialized(m);
  parlay::parallel_for(0, l, [&](std::size_t b) {
    std::size_t k = sums[b];
    for (std::size_t i = b * block; i < std::min(n, (b + 1) * block); i++)
      if (keep(i)) out[k++] = f(i);
  }, 1);
  return out;
}

// A sparse state: (basis index, weight) pairs sorted by index, with no
// duplicate indices.
class SortedState {
 public:
  // Below this many elements, merges run sequentially.
  static constexpr std::size_t merge_grain = 2048;

  SortedState() = default;

  static SortedState singleton(WeightedIdx w) {
    SortedState s;
    s.data_ = seq<WeightedIdx>(1, w);
    return s;
  }

  const seq<WeightedIdx>& entries() const { return data_; }
  bool empty() const { return data_.empty(); }

  // Drop entries whose accumulated weight is zero (parlay::filter).
  SortedState without_zeros() && {
    data_ = pack<WeightedIdx>(
        data_.size(), [&](std::size_t i) { return data_[i]; },
        [&](std::size_t i) { return !data_[i].weight.is_zero(); });
    return std::move(*this);
  }

  // Merge two states, adding the weights of indices that appear in both.
  static SortedState merge(SortedState&& a, SortedState&& b) {
    if (a.empty()) return std::move(b);
    if (b.empty()) return std::move(a);

    // 1. Divide-and-conquer merge into one sorted array; an index present in
    //    both inputs ends up in two adjacent slots.
    std::size_t n = a.data_.size() + b.data_.size();
    auto merged = seq<WeightedIdx>::uninitialized(n);
    merge_into(a.data_.data(), a.data_.size(), b.data_.data(), b.data_.size(), merged.data());

    // 2. Combine adjacent duplicates, dropping any whose weights cancel to
    //    zero. Each index occurs at most twice.
    auto same_as_prev = [&](std::size_t i) {
      return i > 0 && merged[i].idx == merged[i - 1].idx;
    };
    auto combine = [&](std::size_t i) {
      WeightedIdx w = merged[i];
      if (i + 1 < n && merged[i + 1].idx == w.idx) w.weight = w.weight + merged[i + 1].weight;
      return w;
    };
    auto keep = [&](std::size_t i) { return !same_as_prev(i) && !combine(i).weight.is_zero(); };
    SortedState result;
    if (n <= merge_grain) {
      for (std::size_t i = 0; i < n; i++)
        if (keep(i)) result.data_.push_back(combine(i));
    } else {
      // parlay::pack_index, then parlay::tabulate
      auto starts = pack<std::size_t>(n, [](std::size_t i) { return i; }, keep);
      result.data_ = seq<WeightedIdx>::from_function(
          starts.size(), [&](std::size_t k) { return combine(starts[k]); });
    }
    return result;
  }

 private:
  static bool idx_less(const WeightedIdx& x, const WeightedIdx& y) { return x.idx < y.idx; }

  // Parallel merge of sorted a and b into out (|out| = na + nb): split the
  // larger input at its midpoint and binary-search the other. Equal indices
  // from the two inputs always land next to each other.
  static void merge_into(const WeightedIdx* a, std::size_t na, const WeightedIdx* b,
                         std::size_t nb, WeightedIdx* out) {
    if (na + nb <= merge_grain) {
      std::merge(a, a + na, b, b + nb, out, idx_less);
      return;
    }
    if (na < nb) {
      std::swap(a, b);
      std::swap(na, nb);
    }
    std::size_t ma = na / 2;
    std::size_t mb = std::lower_bound(b, b + nb, a[ma], idx_less) - b;
    out[ma + mb] = a[ma];
    parlay::par_do(
        [&] { merge_into(a, ma, b, mb, out); },
        [&] { merge_into(a + ma + 1, na - ma - 1, b + mb, nb - mb, out + ma + mb + 1); });
  }

  seq<WeightedIdx> data_;
};

// A gate, as a "push" action: it maps one weighted basis index to one or two
// successors.
struct Gate {
  enum class Kind {
    Single,  // 2x2 unitary on q[0]
    CX,      // flip q[1] if q[0]                 (cx)
    CPhase,  // multiply by phase if q[0] and q[1] (cz)
  };

  Kind kind;
  std::array<int, 3> q{};
  // Single: matrix [[m[0], m[1]], [m[2], m[3]]], i.e.
  //   |0> -> m[0]|0> + m[2]|1>,   |1> -> m[1]|0> + m[3]|1>
  // CPhase: the phase is m[0].
  std::array<Complex, 4> m{};

  static Gate x(int t) { return {Kind::Single, {t, 0, 0}, {{{0, 0}, {1, 0}, {1, 0}, {0, 0}}}}; }
  static Gate ry(int t, double theta) {
    double c = std::cos(theta / 2), s = std::sin(theta / 2);
    return {Kind::Single, {t, 0, 0}, {{{c, 0}, {-s, 0}, {s, 0}, {c, 0}}}};
  }
  static Gate cx(int c, int t) { return {Kind::CX, {c, t, 0}, {}}; }
  static Gate cz(int a, int b) { return {Kind::CPhase, {a, b, 0}, {{{-1, 0}}}}; }

  // Can this gate turn one basis index into two?
  bool branching() const {
    if (kind != Kind::Single) return false;
    auto nz = [](Complex c) { return c.re != 0 || c.im != 0; };
    return (nz(m[0]) && nz(m[2])) || (nz(m[1]) && nz(m[3]));
  }

  // Writes the successors of `in` to out[0..k) and returns k (1 or 2).
  int apply(const WeightedIdx& in, WeightedIdx out[2]) const {
    BasisIdx x = in.idx;
    auto bit = [x](int qi) { return ((x >> qi) & 1) != 0; };
    auto mask = [](int qi) { return BasisIdx{1} << qi; };
    switch (kind) {
      case Kind::Single: {
        BasisIdx x0 = x & ~mask(q[0]);
        BasisIdx x1 = x | mask(q[0]);
        // the column of the matrix for the current value of the qubit
        Complex to0 = bit(q[0]) ? m[1] : m[0];
        Complex to1 = bit(q[0]) ? m[3] : m[2];
        int k = 0;
        if (to0.re != 0 || to0.im != 0) out[k++] = {x0, to0 * in.weight};
        if (to1.re != 0 || to1.im != 0) out[k++] = {x1, to1 * in.weight};
        return k;
      }
      case Kind::CX:
        out[0] = {bit(q[0]) ? x ^ mask(q[1]) : x, in.weight};
        return 1;
      case Kind::CPhase:
        out[0] = {x, bit(q[0]) && bit(q[1]) ? m[0] * in.weight : in.weight};
        return 1;
    }
    return 0;
  }
};

struct MergeFeynResult {
  SortedState state;
  std::int64_t num_gate_apps = 0;  // total number of single-path gate applications
};

// MergeFeyn(gates, initial, n, grain) pushes every element of initial[0..n)
// through `gates` and returns the resulting state.
//
//   - The input elements are split in half, both halves are expanded in
//     parallel, and the two resulting states are merged.
//   - Each element is pushed through the gates one at a time; at a branching
//     gate both children are expanded in parallel and their results merged.
//
// A subproblem forks only if its estimated number of output paths (input
// elements * 2^(branching gates remaining)) exceeds `grain`; otherwise it runs
// the same recursion sequentially. grain = 0 forks everywhere.
//
// The two results of a fork stay live until their merge has been built, so a
// merge holds its inputs and its output at once.
inline MergeFeynResult MergeFeyn(const std::vector<Gate>& gates, const WeightedIdx* initial,
                                 std::int64_t n, std::int64_t grain) {
  const int num_gates = static_cast<int>(gates.size());

  // branching_after[g]: number of branching gates in gates[g..]. Like the
  // gates themselves it is input, so it is left uncharged.
  std::vector<int> branching_after(num_gates + 1, 0);
  for (int g = num_gates - 1; g >= 0; g--)
    branching_after[g] = branching_after[g + 1] + (gates[g].branching() ? 1 : 0);

  auto should_fork = [&](std::int64_t count, int gatenum) {
    return std::ldexp(static_cast<double>(count), branching_after[gatenum]) >
           static_cast<double>(grain);
  };

  // Expand two subproblems (in parallel if `fork`) and merge the results.
  auto both = [](bool fork, auto&& left, auto&& right) -> MergeFeynResult {
    MergeFeynResult l, r;
    if (fork) {
      parlay::par_do([&] { l = left(); }, [&] { r = right(); });
    } else {
      l = left();
      r = right();
    }
    return {SortedState::merge(std::move(l.state), std::move(r.state)),
            l.num_gate_apps + r.num_gate_apps};
  };

  // Push one weighted index through gates[gatenum..].
  auto expand_elem = [&](auto& self, WeightedIdx w, int gatenum) -> MergeFeynResult {
    std::int64_t apps = 0;
    while (true) {
      if (w.weight.is_zero()) return {SortedState{}, apps};
      if (gatenum >= num_gates) return {SortedState::singleton(w), apps};
      WeightedIdx out[2];
      int k = gates[gatenum].apply(w, out);
      apps++;
      gatenum++;
      if (k == 1) {
        w = out[0];
        continue;
      }
      MergeFeynResult e = both(
          should_fork(2, gatenum), [&] { return self(self, out[0], gatenum); },
          [&] { return self(self, out[1], gatenum); });
      e.num_gate_apps += apps;
      return e;
    }
  };

  // Expand input elements [lo, hi).
  auto expand_range = [&](auto& self, std::int64_t lo, std::int64_t hi) -> MergeFeynResult {
    if (hi <= lo) return {};
    if (hi - lo == 1) return expand_elem(expand_elem, initial[lo], 0);
    std::int64_t mid = lo + (hi - lo) / 2;
    return both(
        should_fork(hi - lo, 0), [&] { return self(self, lo, mid); },
        [&] { return self(self, mid, hi); });
  };

  MergeFeynResult result = expand_range(expand_range, 0, n);
  result.state = std::move(result.state).without_zeros();
  return result;
}

}  // namespace mergefeyn

#endif  // SPLANG_BENCH_MERGEFEYN_H_
