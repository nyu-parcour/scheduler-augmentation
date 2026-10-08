// k-clique counting, measured with scheduler augmentation.
//
// A port of ARB-COUNT with REC-COUNT-CLIQUES (Algorithm 1) from Shi, Dhulipala
// and Shun, "Parallel Clique Counting and Peeling Algorithms" (arXiv
// 2002.10047), written from the paper. Only the total count is computed; there
// is no peeling and no per-vertex count.
//
// The graph is oriented by degree, u -> v iff (deg u, u) < (deg v, v), so that
// every k-clique is counted once, from its lowest-ranked vertex, and every
// out-degree is at most Delta+ (on the order of sqrt(m)). The count is the sum
// over v of rec(N+(v), k-1), where rec(I, l) counts the l-cliques in the set
// I: it forks over the positions of I by halving the range down to single
// positions i, and at each one allocates I' = I cap N+(I[i]), which stays live
// for the whole recursive subtree below it and is freed once that subtree
// joins. rec(I, 1) is |I|.
//
// That I' is the reason R1star and Rinf separate. One processor holds one I'
// per level of a single root-to-leaf path, k-2 of them of at most Delta+
// elements, so R1star <= (k-2)(Delta+ + 1) cells, the +1 being the capacity
// word space_sequence prepends. An unbounded number hold one per node of the
// whole recursion tree, which grows with the number of cliques. With
// --fused-base the last level counts |I cap N+(I[i])| with a merge instead of
// materializing it, which removes one level: R1star <= (k-3)(Delta+ + 1), and
// at k = 3 nothing is allocated at all.
//
// The oriented graph is built outside the augmented region and never charged,
// so S = 0. With --orient-inside the same orientation runs inside it instead:
// its two arrays, offsets and out-edges, are space_sequences allocated on the
// spine, where they make up S. The rank by degree is never materialized: it is
// compared directly as (degree, id), so there is no rank array and no sort.
//
// Input is a symmetric PBBS AdjacencyGraph; the vertex id type is vid.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <parlay/io.h>
#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"

namespace {

using vid = std::int64_t;      // vertex id: one cell
using eid = std::int64_t;      // edge offset
using count_t = std::uint64_t;

template <typename T>
using seq = parlay::space_sequence<T>;
template <typename T>
using plain_seq = parlay::sequence<T>;

// Granularity of the orientation's loops, none of whose iterations allocate.
constexpr long orient_grain = 1024;

// The symmetric input graph in CSR, never charged.
struct graph_t {
  vid n = 0;
  eid m = 0;
  plain_seq<eid> offsets;  // n+1
  plain_seq<vid> edges;    // m
  eid degree(vid v) const { return offsets[v + 1] - offsets[v]; }
};

graph_t read_adjacency_graph(const char* path) {
  static constexpr char header[] = "AdjacencyGraph";
  constexpr long header_len = sizeof(header) - 1;
  const parlay::chars str = parlay::chars_from_file(path);
  if (static_cast<long>(str.size()) < header_len ||
      !std::equal(header, header + header_len, str.begin())) {
    std::fprintf(stderr, "error: %s is not a PBBS AdjacencyGraph\n", path);
    std::exit(1);
  }
  auto words = parlay::map_tokens(parlay::make_slice(str).cut(header_len, str.size()),
                                  [](auto s) {
                                    eid r = 0;
                                    for (char c : s) r = 10 * r + (c - '0');
                                    return r;
                                  });
  graph_t g;
  g.n = words.size() >= 2 ? words[0] : -1;
  g.m = words.size() >= 2 ? words[1] : -1;
  if (g.n < 0 || static_cast<long>(words.size()) != 2 + g.n + g.m) {
    std::fprintf(stderr, "error: %s: header does not match its length\n", path);
    std::exit(1);
  }
  g.offsets = plain_seq<eid>::uninitialized(g.n + 1);
  parlay::parallel_for(0, g.n, [&](long i) { g.offsets[i] = words[2 + i]; });
  g.offsets[g.n] = g.m;
  g.edges = parlay::tabulate(g.m, [&](long i) { return static_cast<vid>(words[2 + g.n + i]); });
  return g;
}

// The degree-oriented DAG: u -> v iff u comes before v in the order by
// (degree, id). Out-lists are sorted by id, so that intersecting two of them
// is a merge. Seq is plain_seq outside the augmented region and seq inside
// it, and nothing else differs.
template <template <typename> class Seq>
struct dag_t {
  vid n = 0;
  Seq<eid> offsets;  // n+1
  Seq<vid> edges;    // m/2
};

// rank[u] < rank[v] for the rank by (degree, id), without the rank array.
bool before(const graph_t& g, vid u, vid v) {
  const eid du = g.degree(u), dv = g.degree(v);
  return du < dv || (du == dv && u < v);
}

template <template <typename> class Seq>
dag_t<Seq> orient(const graph_t& g) {
  const vid n = g.n;
  dag_t<Seq> d;
  d.n = n;

  // out-degrees, then their exclusive scan, which leaves m/2 in offsets[n]
  d.offsets = Seq<eid>::uninitialized(n + 1);
  parlay::parallel_for(0, n, [&](long u) {
    eid c = 0;
    for (eid j = g.offsets[u]; j < g.offsets[u + 1]; j++) c += before(g, u, g.edges[j]);
    d.offsets[u] = c;
  }, orient_grain);
  d.offsets[n] = 0;
  const eid m_out = parlay::scan_inplace(d.offsets);

  d.edges = Seq<vid>::uninitialized(m_out);
  parlay::parallel_for(0, n, [&](long u) {
    eid k = d.offsets[u];
    for (eid j = g.offsets[u]; j < g.offsets[u + 1]; j++) {
      const vid v = g.edges[j];
      if (before(g, u, v)) d.edges[k++] = v;
    }
    std::sort(d.edges.begin() + d.offsets[u], d.edges.begin() + k);
  }, orient_grain);
  return d;
}

// What the counting reads of the DAG, whichever sequence type holds it.
struct dag_view {
  vid n;
  const eid* offsets;
  const vid* edges;
  const vid* out(vid v) const { return edges + offsets[v]; }
  eid out_degree(vid v) const { return offsets[v + 1] - offsets[v]; }
};

template <template <typename> class Seq>
dag_view view(const dag_t<Seq>& d) {
  return {d.n, d.offsets.data(), d.edges.data()};
}

eid max_out_degree(const dag_view& d) {
  if (d.n == 0) return 0;
  return parlay::reduce(parlay::delayed_seq<eid>(d.n, [&](long v) { return d.out_degree(v); }),
                        parlay::maxm<eid>());
}

// Sum leaf(i) over [lo, hi), halving the range down to single positions, as
// nqueens does with its column range.
//
// The par_do is conservative: a worker whose right half was stolen waits for
// it instead of stealing other work. A non-conservative join runs whatever it
// steals on top of the waiting frame, whose I' stays live underneath, so one
// worker can hold several root-to-leaf paths at once; on com-dblp at k = 4
// the observed HWM then exceeds S + P*R1star by up to 1.4x.
template <typename Leaf>
count_t sum_range(long lo, long hi, const Leaf& leaf) {
  if (hi - lo <= 0) return 0;
  if (hi - lo == 1) return leaf(lo);
  const long mid = lo + (hi - lo) / 2;
  count_t a = 0, b = 0;
  parlay::par_do([&]() { a = sum_range(lo, mid, leaf); },
                 [&]() { b = sum_range(mid, hi, leaf); }, /*conservative=*/true);
  return a + b;
}

// Sorted merge of a and b, writing a cap b to out and returning its size.
long intersect(const vid* a, long na, const vid* b, long nb, vid* out) {
  long i = 0, j = 0, k = 0;
  while (i < na && j < nb) {
    if (a[i] < b[j]) i++;
    else if (b[j] < a[i]) j++;
    else { out[k++] = a[i]; i++; j++; }
  }
  return k;
}

// The same merge, only counting.
long intersect_count(const vid* a, long na, const vid* b, long nb) {
  long i = 0, j = 0, k = 0;
  while (i < na && j < nb) {
    if (a[i] < b[j]) i++;
    else if (b[j] < a[i]) j++;
    else { k++; i++; j++; }
  }
  return k;
}

struct counter {
  dag_view g;
  bool fused_base;

  // REC-COUNT-CLIQUES: the number of l-cliques in I, a sorted set of len
  // vertices that are all out-neighbors of every vertex of the clique so far.
  // I is a view into memory the caller keeps live; it is never copied.
  count_t rec(const vid* I, long len, long l) const {
    if (l == 1) return static_cast<count_t>(len);
    if (fused_base && l == 2) {
      return sum_range(0, len, [&](long i) -> count_t {
        const vid v = I[i];
        return static_cast<count_t>(intersect_count(I, len, g.out(v), g.out_degree(v)));
      });
    }
    return sum_range(0, len, [&](long i) -> count_t {
      const vid v = I[i];
      const long dv = g.out_degree(v);
      auto I2 = seq<vid>::uninitialized(std::min(len, dv));
      const long len2 = intersect(I, len, g.out(v), dv, I2.data());
      // I2 is destroyed at the end of this scope, that is after the recursion
      // has joined, so it stays live across every fork the subtree performs.
      return len2 >= l - 1 ? rec(I2.data(), len2, l - 1) : 0;
    });
  }

  // ARB-COUNT: every k-clique, counted from its lowest-ranked vertex. A vertex
  // with fewer than k-1 out-neighbors starts none, as in rec.
  count_t count(long k) const {
    return sum_range(0, g.n, [&](long v) -> count_t {
      const long dv = g.out_degree(v);
      return dv >= k - 1 ? rec(g.out(v), dv, k - 1) : 0;
    });
  }
};

}  // namespace

int main(int argc, char** argv) {
  auto opts = splang_bench::parse_args(argc, argv, 0);
  if (opts.graph == nullptr || opts.k < 3) {
    std::fprintf(stderr, "usage: %s --k K [--fused-base] [--orient-inside] <graph.adj>   (K >= 3)\n",
                 argv[0]);
    return 2;
  }
  const long k = static_cast<long>(opts.k);
  opts.size = opts.k;  // the CSV's size column

  const graph_t g = read_adjacency_graph(opts.graph);

  dag_t<plain_seq> outside;
  eid dplus = 0;
  if (!opts.orient_inside) {
    outside = orient<plain_seq>(g);
    dplus = max_out_degree(view(outside));
  }

  count_t count = 0;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() {
    if (opts.orient_inside) {
      const dag_t<seq> inside = orient<seq>(g);
      dplus = max_out_degree(view(inside));
      count = counter{view(inside), opts.fused_base}.count(k);
    }
    else {
      count = counter{view(outside), opts.fused_base}.count(k);
    }
  });
  const double ms = t.ms();

  std::fprintf(stderr, "count %llu n %lld m %lld dplus %lld\n",
               static_cast<unsigned long long>(count), static_cast<long long>(g.n),
               static_cast<long long>(g.m), static_cast<long long>(dplus));
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(count));
  // At k = 3 with --fused-base, and the DAG built outside, nothing is allocated.
  const bool expect_alloc = !(opts.fused_base && k == 3 && !opts.orient_inside);
  splang_bench::report("kclique", opts, buf, v, ms, expect_alloc);
  return 0;
}
