// Approximate nearest neighbor search over a proximity graph, measured with
// scheduler augmentation.
//
// A port of the query phase of pbbs's ANN benchmarks, `searchAll` and
// `beam_search` in pbbs/benchmarks/ANN/utils/beamSearch.h. Every query runs a
// beam search of width L in one parallel_for iteration, and each search
// allocates the same scratch: a hash table of 2^(ceil(log2 L^2) - 2) ints for
// the visited filter, the frontier and a buffer for merging into it, and the
// list of visited points, which grows by one per step. All of it is freed when
// the query finishes. That is the shape S + P*R1star_partial is tight for: a
// flat loop of many iterations, each peaking at about the same size, so P
// workers each hold one search at a time while an unbounded number hold one
// per query.
//
// What a query keeps is its k neighbors. pbbs allocates them inside the
// iteration (q[i]->ngh = neighbors), which puts all n_q of them in the loop's
// R1star, where the bound multiplies them by P although they only ever exist
// once. Built with -DANN_PREALLOCATE the output rows are allocated before the
// loop instead, so they land in S, and each iteration only writes its row.
// Nothing else differs between the two builds.
//
// The index is built outside the augmented region, since only the search is
// measured: n random points in the unit cube, each linked to its nearest
// neighbors plus a few random points for long-range edges. --size is the
// number of queries. As in strassen, the computation then copies the index
// and the queries into charged memory before searching, serially so that the
// copies add no forks. They are live across the whole search and every worker
// reads them, so they land in S and are counted once, where P*R1star would
// count them once per worker.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"

namespace {

constexpr long dims = 16;           // coordinates per point
constexpr long num_points = 20000;  // points in the index
constexpr long near_degree = 24;    // edges to nearest neighbors
constexpr long far_degree = 8;      // edges to random points
constexpr long degree = near_degree + far_degree;
constexpr int beam_size = 256;      // L
constexpr int k = 10;               // neighbors reported per query

using pid = std::pair<int, float>;  // point id and distance from the query

template <typename T>
using seq = parlay::space_sequence<T>;

// Euclidean distance
float distance(const float* a, const float* b) {
  float r = 0;
  for (long i = 0; i < dims; i++) {
    float d = a[i] - b[i];
    r += d * d;
  }
  return r;
}

// The index: coordinates and out-edges, both flat.
template <typename Floats, typename Ints>
struct basic_index {
  Floats coords;  // num_points * dims
  Ints edges;     // num_points * degree
  const float* point(long i) const { return coords.data() + i * dims; }
  const int* out(long i) const { return edges.data() + i * degree; }
};
// As built, never charged.
using index_t = basic_index<parlay::sequence<float>, parlay::sequence<int>>;
// The copy the search runs on, charged.
using charged_index_t = basic_index<seq<float>, seq<int>>;

// A serial, charged copy of s.
template <typename T>
seq<T> charged_copy(const parlay::sequence<T>& s) {
  auto c = seq<T>::uninitialized(s.size());
  std::copy(s.begin(), s.end(), c.begin());
  return c;
}

parlay::sequence<float> random_points(long n, long seed) {
  parlay::random_generator gen(seed);
  std::uniform_real_distribution<float> dis(0.0, 1.0);
  return parlay::tabulate(n * dims, [&](long i) { auto r = gen[i]; return dis(r); });
}

index_t build_index() {
  index_t g{random_points(num_points, 0), parlay::sequence<int>(num_points * degree)};
  parlay::random_generator gen(1);
  std::uniform_int_distribution<int> dis(0, num_points - 1);
  parlay::parallel_for(0, num_points, [&](long u) {
    // exact nearest neighbors by brute force, then random long-range edges
    std::vector<pid> d(num_points);
    for (long v = 0; v < num_points; v++)
      d[v] = {static_cast<int>(v), v == u ? std::numeric_limits<float>::max()
                                          : distance(g.point(u), g.point(v))};
    std::nth_element(d.begin(), d.begin() + near_degree, d.end(),
                     [](pid a, pid b) { return a.second < b.second; });
    int* e = g.edges.data() + u * degree;
    for (long j = 0; j < near_degree; j++) e[j] = d[j].first;
    auto r = gen[u];
    for (long j = near_degree; j < degree; j++) e[j] = dis(r);
  }, 1);
  return g;
}

// beam_search from beamSearch.h returning the
// final frontier.
template <typename Index>
seq<pid> beam_search(const float* p, const Index& g, int start) {
  auto less = [](pid a, pid b) {
    return a.second < b.second || (a.second == b.second && a.first < b.first);
  };
  auto make_pid = [&](int q) { return pid{q, distance(g.point(q), p)}; };

  seq<pid> visited;
  int bits = std::ceil(std::log2(beam_size * beam_size)) - 2;
  seq<int> hash_table(1 << bits, -1);

  seq<pid> pre_frontier(1, make_pid(start));
  seq<pid> frontier = pre_frontier;  // parlay::sort's output
  std::sort(frontier.begin(), frontier.end(), less);

  seq<pid> unvisited_frontier(beam_size);
  seq<pid> new_frontier(beam_size + degree);
  unvisited_frontier[0] = frontier[0];
  int remain = 1;

  // terminate beam search when the entire frontier has been visited
  while (remain > 0) {
    // the next node to visit is the unvisited frontier node closest to p
    pid current = unvisited_frontier[0];
    const int* nbh = g.out(current.first);

    // parlay::filter, which at this length runs serially; its output buffer
    // is sized for every edge, and the kept ones are counted into it
    seq<int> candidates = seq<int>::uninitialized(degree);
    long nc = 0;
    for (long j = 0; j < degree; j++) {
      int a = nbh[j];
      int loc = parlay::hash64_2(a) & ((1 << bits) - 1);
      if (hash_table[loc] == a) continue;
      hash_table[loc] = a;
      candidates[nc++] = a;
    }
    seq<pid> pair_candidates = seq<pid>::uninitialized(nc);
    for (long j = 0; j < nc; j++) pair_candidates[j] = make_pid(candidates[j]);
    seq<pid> sorted_candidates = pair_candidates;  // parlay::sort's output
    std::sort(sorted_candidates.begin(), sorted_candidates.end(), less);

    auto f_iter = std::set_union(frontier.begin(), frontier.end(),
                                 sorted_candidates.begin(), sorted_candidates.end(),
                                 new_frontier.begin(), less);
    size_t f_size = std::min<size_t>(beam_size, f_iter - new_frontier.begin());
    frontier = seq<pid>(new_frontier.begin(), new_frontier.begin() + f_size);
    visited.insert(std::upper_bound(visited.begin(), visited.end(), current, less), current);
    auto uf_iter = std::set_difference(frontier.begin(), frontier.end(),
                                       visited.begin(), visited.end(),
                                       unvisited_frontier.begin(), less);
    remain = uf_iter - unvisited_frontier.begin();
  }
  seq<pid> visited_copy = visited;  // parlay::to_sequence(visited)
  splang_bench::do_not_optimize(visited_copy.data());
  return frontier;
}

}  // namespace

int main(int argc, char** argv) {
  const auto opts = splang_bench::parse_args(argc, argv, 10000);
  const long nq = static_cast<long>(opts.size);

  const index_t g = build_index();
  const auto queries = random_points(nq, 2);
  const int start = 0;

  seq<seq<int>> ngh;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() {
    const charged_index_t index{charged_copy(g.coords), charged_copy(g.edges)};
    const seq<float> qs = charged_copy(queries);
    ngh = seq<seq<int>>(nq);
    for (long i = 0; i < nq; i++) ngh[i] = seq<int>(k);
    // searchAll
    parlay::parallel_for(0, nq, [&](long i) {
      seq<int>& neighbors = ngh[i];
      seq<pid> beam = beam_search(qs.data() + i * dims, index, start);
      for (int j = 0; j < k; j++) neighbors[j] = beam[j].first;
    }, 1);
  });
  const double ms = t.ms();

  // Recall@k against brute force, on a sample of the queries.
  const long trials = std::min<long>(nq, 50);
  long hits = 0;
  for (long i = 0; i < trials; i++) {
    const float* q = queries.data() + i * dims;
    std::vector<pid> d(num_points);
    for (long u = 0; u < num_points; u++) d[u] = {static_cast<int>(u), distance(g.point(u), q)};
    std::partial_sort(d.begin(), d.begin() + k, d.end(),
                      [](pid a, pid b) { return a.second < b.second; });
    for (int j = 0; j < k; j++)
      hits += std::count(ngh[i].begin(), ngh[i].end(), d[j].first);
  }
  const double recall = static_cast<double>(hits) / (trials * k);
  const bool correct = recall > 0.8;

  char value[64];
  std::snprintf(value, sizeof(value), "%s(recall %.3f)", correct ? "" : "WRONG ", recall);
  splang_bench::report("ann", opts, value, v, ms);
  if (!correct) std::fprintf(stderr, "recall %.3f\n", recall);
  return correct ? 0 : 1;
}
