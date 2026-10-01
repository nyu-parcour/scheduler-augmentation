// par-clique: k-clique counting by ARB-COUNT (Algorithm 1 of Shi, Dhulipala
// and Shun, "Parallel Clique Counting and Peeling Algorithms", arXiv
// 2002.10047), instrumented to measure R1, R* and Rinf.
//
//   REC-COUNT-CLIQUES(DG, I, l):
//     if l = 1: return |I|
//     T <- array of size |I|
//     parfor v in I:
//       I' <- INTERSECT(I, N+(v))
//       T[v] <- REC-COUNT-CLIQUES(DG, I', l-1)
//     return REDUCE-ADD(T)
//
//   ARB-COUNT(G, k, ORIENT):
//     DG <- ORIENT(G)
//     return REC-COUNT-CLIQUES(DG, V, k)
//
// What is charged is exactly what the pseudocode allocates: each parfor
// iteration's I' (a space_sequence of |I n N+(v)| ids, freed once the
// recursion below it has joined), and each T below the top level. --T is the
// ablation of where T is materialized: none sums every level as a reduce over
// a delayed sequence, all also materializes the n-sized top-level T. Loading
// and orienting the graph happen before the measured region and are not
// charged; --charge-graph is the ablation that copies the oriented graph into
// the region and counts on the copy.
//
// --variant induced replaces everything below the top level with GBBS's
// practical scheme: one workspace per top-level vertex, holding the subgraph
// induced on its out-neighbourhood (out-degree squared), with the recursion
// run serially inside it.
//
// The fork tree follows the other ports here (allocfree, nqueens): every
// parfor and REDUCE-ADD is a binary par_do split at the midpoint down to single
// iterations, so the graph being measured is the pseudocode's. --grain G is an
// ablation that runs blocks of up to G iterations serially instead.
//
// Joins use parlay's default: a worker whose right branch was stolen steals
// other work while it waits, on top of the stalled frame. That can bury a
// frame that is ready to resume, holding its I' and T with no processor on
// it, so footprint <= P * R* is not guaranteed. --join wait is the ablation
// that keeps the busy-leaves property: blocked workers spin instead. Nothing
// inside the measured region depends on timing, P or worker_id, so R1, R*,
// Rinf and delta are properties of (graph, k, flags) alone.
//
// See PLAN.md in space-bound-profiling for the flags and what each ablates.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"

namespace {

using vid = std::uint32_t;
using count_t = std::uint64_t;

// ---------------------------------------------------------------------------
// Command line

struct config {
  std::string graph_file;
  bool rmat = false;
  std::uint64_t rmat_n = 0, rmat_m = 0, rmat_seed = 0;
  long k = 0;
  std::string variant = "faithful";
  std::string t_mode = "inner";
  std::string order = "degree";
  bool early_base = true;
  bool prune = false;
  bool id64 = false;
  std::size_t grain = 1;
  bool join_wait = false;
  bool footprint = true;
  bool charge_graph = false;
  bool json = false;
  std::string write_graph;
};

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
      "usage: %s (--graph FILE | --rmat N,M,SEED) --k K [options]\n"
      "  --graph FILE          PBBS AdjacencyGraph text (symmetrized, deduplicated on load)\n"
      "  --rmat N,M,SEED       RMAT graph on 2^round(log2 N) vertices from M edge samples\n"
      "  --k K                 clique size, K >= 4\n"
      "  --variant faithful    Algorithm 1 as written (default)\n"
      "  --variant induced     GBBS -space 6 --saveSpace: per top-level vertex, a workspace\n"
      "                        holding the subgraph induced on N+(v), recursed serially\n"
      "  --T none|inner|all    where T is materialized: nowhere, below the top level\n"
      "                        (default), or at every level including the n-sized top\n"
      "  --order degree|kcore  rank by (degree, id) (default), or by k-core peel\n"
      "  --early-base on|off   at l = 2 count |I n N+(v)| without building I' (default on)\n"
      "  --prune off|on        skip I' when |I'| < l-1 (default off)\n"
      "  --id u32|i64          element type of I' (default u32)\n"
      "  --grain G             serial block size of every parfor/reduce (default 1)\n"
      "  --join steal|wait     blocked joins steal other work (default) or spin\n"
      "  --charge-graph off|on copy the oriented graph inside the measured region, so it is\n"
      "                        charged (default off)\n"
      "  --footprint on|off    track the global footprint (default on); off for timing runs\n"
      "  --write-graph FILE    also write the symmetrized input as AdjacencyGraph text\n"
      "  --json                one JSON line instead of text\n",
      argv0);
  std::exit(2);
}

bool on_off(const char* argv0, const char* s) {
  if (std::strcmp(s, "on") == 0) return true;
  if (std::strcmp(s, "off") == 0) return false;
  usage(argv0);
}

config parse(int argc, char** argv) {
  config c;
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    auto next = [&]() -> const char* {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (std::strcmp(a, "--graph") == 0) c.graph_file = next();
    else if (std::strcmp(a, "--rmat") == 0) {
      unsigned long long n, m, s;
      if (std::sscanf(next(), "%llu,%llu,%llu", &n, &m, &s) != 3) usage(argv[0]);
      c.rmat = true;
      c.rmat_n = n;
      c.rmat_m = m;
      c.rmat_seed = s;
    }
    else if (std::strcmp(a, "--k") == 0) c.k = std::atol(next());
    else if (std::strcmp(a, "--variant") == 0) c.variant = next();
    else if (std::strcmp(a, "--T") == 0) c.t_mode = next();
    else if (std::strcmp(a, "--order") == 0) c.order = next();
    else if (std::strcmp(a, "--early-base") == 0) c.early_base = on_off(argv[0], next());
    else if (std::strcmp(a, "--prune") == 0) c.prune = on_off(argv[0], next());
    else if (std::strcmp(a, "--id") == 0) {
      const std::string s = next();
      if (s == "u32") c.id64 = false;
      else if (s == "i64") c.id64 = true;
      else usage(argv[0]);
    }
    else if (std::strcmp(a, "--grain") == 0) c.grain = std::strtoull(next(), nullptr, 10);
    else if (std::strcmp(a, "--join") == 0) {
      const std::string s = next();
      if (s == "steal") c.join_wait = false;
      else if (s == "wait") c.join_wait = true;
      else usage(argv[0]);
    }
    else if (std::strcmp(a, "--charge-graph") == 0) c.charge_graph = on_off(argv[0], next());
    else if (std::strcmp(a, "--footprint") == 0) c.footprint = on_off(argv[0], next());
    else if (std::strcmp(a, "--write-graph") == 0) c.write_graph = next();
    else if (std::strcmp(a, "--json") == 0) c.json = true;
    else usage(argv[0]);
  }

  if (c.graph_file.empty() == !c.rmat) usage(argv[0]);
  if (c.k < 4) usage(argv[0]);
  if (c.grain < 1) usage(argv[0]);
  if (c.variant != "faithful" && c.variant != "induced") usage(argv[0]);
  if (c.t_mode != "none" && c.t_mode != "inner" && c.t_mode != "all") usage(argv[0]);
  if (c.order != "degree" && c.order != "kcore") usage(argv[0]);
  // --variant induced labels each vertex with a recursion level, up to k-2,
  // in a byte under --id u32.
  if (c.variant == "induced" && !c.id64 && c.k - 2 > 255) {
    std::fprintf(stderr, "error: --variant induced --id u32 needs k <= 257\n");
    std::exit(2);
  }
  return c;
}

// ---------------------------------------------------------------------------
// Graphs (all outside the measured region, in plain uncharged sequences)

// Compressed sparse rows: the neighbours of v are adj[off[v] .. off[v+1]),
// sorted ascending.
struct csr {
  std::size_t n = 0;
  parlay::sequence<std::uint64_t> off;
  parlay::sequence<vid> adj;

  std::size_t degree(std::size_t v) const { return off[v + 1] - off[v]; }
  const vid* neighbors(std::size_t v) const { return adj.data() + off[v]; }
};

// The oriented graph as the measured computation reads it: the csr built
// outside the region or, under --charge-graph, a charged copy of it.
struct graph_view {
  std::size_t n;
  const std::uint64_t* off;
  const vid* adj;

  std::size_t degree(std::size_t v) const { return off[v + 1] - off[v]; }
  const vid* neighbors(std::size_t v) const { return adj + off[v]; }
};

// A simple undirected graph from an arbitrary edge list: both directions of
// every edge, self-loops dropped, duplicates merged.
csr symmetric_from_edges(std::size_t n, const parlay::sequence<std::pair<vid, vid>>& e) {
  auto arcs = parlay::flatten(parlay::map(e, [](const std::pair<vid, vid>& uv) {
    parlay::sequence<std::pair<vid, vid>> both;
    if (uv.first != uv.second) {
      both.push_back(uv);
      both.push_back({uv.second, uv.first});
    }
    return both;
  }));
  arcs = parlay::unique(parlay::sort(arcs));

  csr g;
  g.n = n;
  g.off = parlay::sequence<std::uint64_t>(n + 1, 0);
  for (const auto& [u, v] : arcs) g.off[u + 1]++;
  for (std::size_t v = 0; v < n; v++) g.off[v + 1] += g.off[v];
  g.adj = parlay::map(arcs, [](const std::pair<vid, vid>& uv) { return uv.second; });
  return g;
}

// PBBS AdjacencyGraph: the header, n, m, then n offsets and m targets.
csr read_adjacency_graph(const std::string& file) {
  std::ifstream in(file);
  if (!in) {
    std::fprintf(stderr, "error: cannot open %s\n", file.c_str());
    std::exit(1);
  }
  std::string header;
  std::uint64_t n = 0, m = 0;
  in >> header >> n >> m;
  if (header != "AdjacencyGraph" || !in) {
    std::fprintf(stderr, "error: %s is not an AdjacencyGraph file\n", file.c_str());
    std::exit(1);
  }
  parlay::sequence<std::uint64_t> off(n + 1);
  for (std::uint64_t v = 0; v < n; v++) in >> off[v];
  off[n] = m;
  parlay::sequence<std::pair<vid, vid>> e(m);
  for (std::uint64_t v = 0; v < n; v++) {
    for (std::uint64_t j = off[v]; j < off[v + 1]; j++) {
      std::uint64_t u;
      in >> u;
      if (u >= n) {
        std::fprintf(stderr, "error: %s: edge target %llu out of range\n", file.c_str(),
                     static_cast<unsigned long long>(u));
        std::exit(1);
      }
      e[j] = {static_cast<vid>(v), static_cast<vid>(u)};
    }
  }
  if (!in) {
    std::fprintf(stderr, "error: %s is truncated\n", file.c_str());
    std::exit(1);
  }
  return symmetric_from_edges(n, e);
}

void write_adjacency_graph(const csr& g, const std::string& file) {
  std::ofstream out(file);
  out << "AdjacencyGraph\n" << g.n << "\n" << g.adj.size() << "\n";
  for (std::size_t v = 0; v < g.n; v++) out << g.off[v] << "\n";
  for (vid u : g.adj) out << u << "\n";
  if (!out) {
    std::fprintf(stderr, "error: cannot write %s\n", file.c_str());
    std::exit(1);
  }
}

// parlaylib's RMAT generator (examples/helper/graph_utils.h, a = .5, b = c =
// .15), seeded. Uniform doubles come from the top 53 bits of the generator so
// that a seed gives the same graph under any standard library.
csr rmat(std::uint64_t n, std::uint64_t m, std::uint64_t seed) {
  const int logn = static_cast<int>(std::lround(std::log2(static_cast<double>(n))));
  const double a = .5, b = .15, c = .15;
  parlay::random_generator gen(seed);
  auto e = parlay::tabulate(m, [&](std::size_t i) {
    auto r = gen[i];
    vid u = 0, v = 0;
    for (int bit = logn - 1; bit >= 0; bit--) {
      const double x = static_cast<double>(r() >> 11) * 0x1.0p-53;
      const vid mask = vid{1} << bit;
      if (x < a) continue;
      if (x < a + b) v |= mask;
      else if (x < a + b + c) u |= mask;
      else {
        u |= mask;
        v |= mask;
      }
    }
    return std::pair<vid, vid>{u, v};
  });
  return symmetric_from_edges(std::size_t{1} << logn, e);
}

// The rank of every vertex when the vertices are sorted by (key(v), v).
template <typename Key>
parlay::sequence<vid> rank_by(std::size_t n, const Key& key) {
  auto keys = parlay::sort(parlay::tabulate(n, [&](std::size_t v) {
    return std::pair{key(v), static_cast<vid>(v)};
  }));
  parlay::sequence<vid> rank(n);
  parlay::parallel_for(0, n, [&](std::size_t i) { rank[keys[i].second] = static_cast<vid>(i); });
  return rank;
}

// Degree order: rank by (degree, id), as GBBS's degreeOrderNodes.
parlay::sequence<vid> degree_rank(const csr& g) {
  return rank_by(g.n, [&](std::size_t v) { return static_cast<std::uint64_t>(g.degree(v)); });
}

// Degeneracy order, from the bucketed parallel peel of parlaylib's
// examples/kcore.h, adapted to record the level and round in which each vertex
// is peeled. Vertices are ranked by (level, round, id). A vertex peeled in some
// round at level l has at most l neighbours not yet peeled when that round
// starts, and those include all of its out-neighbours under this rank, so the
// max out-degree is at most the degeneracy, which is returned in `degeneracy`.
// The rounds depend only on the graph, so the order is deterministic.
//
// GBBS's -o 2 instead sorts vertices by coreness and reads the sorted list as
// a rank; its orientation then has nothing to do with coreness.
parlay::sequence<vid> kcore_rank(const csr& g, std::size_t& degeneracy) {
  const std::size_t n = g.n;
  auto done = parlay::sequence<bool>(n, false);
  auto d = parlay::tabulate(n, [&](std::size_t v) { return static_cast<vid>(g.degree(v)); });
  const vid maxd = (n == 0 ? 0 : parlay::reduce(d, parlay::maximum<vid>())) + 1;
  auto di = parlay::tabulate(n, [&](std::size_t v) { return std::pair(d[v], static_cast<vid>(v)); });
  auto buckets = parlay::map(parlay::group_by_index(di, maxd), [](auto& b) {
    return parlay::sequence<parlay::sequence<vid>>(1, b);
  });
  // (level, round) of every vertex.
  auto peeled = parlay::sequence<std::pair<vid, std::uint64_t>>(n);
  vid k = 0;
  std::size_t total = 0;
  std::uint64_t round = 0;
  degeneracy = 0;

  while (total < n) {
    auto b = parlay::filter(parlay::flatten(buckets[k]),
                            [&](vid v) { return done[v] ? false : (done[v] = true); });
    buckets[k].clear();
    if (b.size() == 0) {
      k++;
      continue;
    }
    total += b.size();
    degeneracy = k;
    parlay::for_each(b, [&](vid v) { peeled[v] = {k, round}; });
    round++;
    auto ngh = parlay::filter(parlay::flatten(parlay::map(b, [&](vid v) {
                                return parlay::to_sequence(parlay::make_slice(
                                    g.neighbors(v), g.neighbors(v) + g.degree(v)));
                              })),
                              [&](vid u) { return d[u] > k; });
    auto u = parlay::map(parlay::histogram_by_key<vid>(ngh), [&](auto uc) {
      auto [u, c] = uc;
      d[u] = std::max(k, d[u] - c);
      return std::pair(d[u], u);
    });
    parlay::for_each(parlay::group_by_key_ordered(u), [&](auto& dv) {
      auto& [dd, vs] = dv;
      buckets[dd].push_back(std::move(vs));
    });
  }
  return rank_by(n, [&](std::size_t v) { return peeled[v]; });
}

// ORIENT: keep the arc u -> v iff rank[u] < rank[v] and both endpoints have
// degree >= k-1 (as GBBS does), and relabel every vertex by its rank.
// Out-neighbour lists come out sorted, and every out-neighbour of v has a
// larger id than v.
csr orient(const csr& g, long k, const parlay::sequence<vid>& rank) {
  const std::size_t n = g.n;
  const std::uint64_t min_deg = static_cast<std::uint64_t>(k - 1);
  auto keep = [&](std::size_t u, vid v) {
    return rank[u] < rank[v] && g.degree(u) >= min_deg && g.degree(v) >= min_deg;
  };

  csr dg;
  dg.n = n;
  dg.off = parlay::sequence<std::uint64_t>(n + 1, 0);
  parlay::parallel_for(0, n, [&](std::size_t u) {
    std::uint64_t d = 0;
    for (std::size_t j = g.off[u]; j < g.off[u + 1]; j++) d += keep(u, g.adj[j]);
    dg.off[rank[u] + 1] = d;
  });
  for (std::size_t v = 0; v < n; v++) dg.off[v + 1] += dg.off[v];
  dg.adj = parlay::sequence<vid>(dg.off[n]);
  parlay::parallel_for(0, n, [&](std::size_t u) {
    vid* out = dg.adj.data() + dg.off[rank[u]];
    std::size_t d = 0;
    for (std::size_t j = g.off[u]; j < g.off[u + 1]; j++) {
      if (keep(u, g.adj[j])) out[d++] = rank[g.adj[j]];
    }
    std::sort(out, out + d);
  });
  return dg;
}

// ---------------------------------------------------------------------------
// The measured computation

// How the fork tree is built: the serial block size, and whether a join whose
// right branch was stolen spins (conservative) rather than stealing.
struct forks {
  std::size_t grain;
  bool conservative;
};

// The fork tree every parfor is built from: split [lo, hi) at the midpoint
// until at most `grain` iterations remain, and run those serially.
template <typename F>
void parfor(std::size_t lo, std::size_t hi, forks fk, const F& f) {
  if (hi - lo <= fk.grain) {
    for (std::size_t i = lo; i < hi; i++) f(i);
    return;
  }
  const std::size_t mid = lo + (hi - lo) / 2;
  parlay::par_do([&]() { parfor(lo, mid, fk, f); },
                 [&]() { parfor(mid, hi, fk, f); }, fk.conservative);
}

// The same fork tree, summing f(i) over [lo, hi): REDUCE-ADD over T, or, where
// T is not materialized, a reduce over the delayed sequence of f.
template <typename F>
count_t parsum(std::size_t lo, std::size_t hi, forks fk, const F& f) {
  if (hi - lo <= fk.grain) {
    count_t s = 0;
    for (std::size_t i = lo; i < hi; i++) s += f(i);
    return s;
  }
  const std::size_t mid = lo + (hi - lo) / 2;
  count_t a = 0, b = 0;
  parlay::par_do([&]() { a = parsum(lo, mid, fk, f); },
                 [&]() { b = parsum(mid, hi, fk, f); }, fk.conservative);
  return a + b;
}

template <typename Id>
class arb_count {
 public:
  arb_count(const graph_view& dg, const config& c)
      : dg_(dg), c_(c), fk_{c.grain, c.join_wait}, k_(static_cast<std::size_t>(c.k)),
        induced_(c.variant == "induced"),
        t_top_(c.t_mode == "all"), t_inner_(c.t_mode != "none") {}

  // REC-COUNT-CLIQUES(DG, V, k). I = V is implicit. Unless --T all, the
  // top-level T is not materialized: its sum is a reduce over the delayed
  // sequence of per-vertex counts. Both variants share this top level.
  count_t run() const {
    return sum(dg_.n, t_top_, [&](std::size_t v) -> count_t {
      const std::size_t d = dg_.degree(v);
      if (c_.prune && d < k_ - 1) return 0;
      return induced_ ? induced_vertex(v, d) : faithful_vertex(v, d);
    });
  }

 private:
  using id_seq = parlay::space_sequence<Id>;
  using count_seq = parlay::space_sequence<count_t>;
  // --variant induced's labels are bytes, as GBBS's are, except under --id
  // i64, where every buffer is in whole cells.
  using label_t = std::conditional_t<std::is_same_v<Id, vid>, std::uint8_t, Id>;
  using label_seq = parlay::space_sequence<label_t>;

  // A top-level iteration of Algorithm 1: INTERSECT(V, N+(v)) = N+(v), copied
  // into a fresh I' like every other iteration's.
  count_t faithful_vertex(std::size_t v, std::size_t d) const {
    auto next = id_seq::uninitialized(d);
    const vid* nb = dg_.neighbors(v);
    for (std::size_t j = 0; j < d; j++) next[j] = static_cast<Id>(nb[j]);
    return rec(next.data(), d, k_ - 1);
    // next is freed here, after the recursion below it has joined.
  }

  // The sum of f(i) over [0, n): parfor into a materialized T, then
  // REDUCE-ADD(T), or, without T, one reduce over the delayed sequence of f.
  // The two have the same fork tree, and the same allocations below it.
  template <typename F>
  count_t sum(std::size_t n, bool materialize, const F& f) const {
    if (!materialize) return parsum(0, n, fk_, f);
    auto T = count_seq::uninitialized(n);
    parfor(0, n, fk_, [&](std::size_t i) { T[i] = f(i); });
    return parsum(0, n, fk_, [&](std::size_t i) { return T[i]; });
    // T is freed here, after the reduce.
  }

  // REC-COUNT-CLIQUES(DG, I, l) below the top level. I is sorted ascending.
  count_t rec(const Id* I, std::size_t n, std::size_t l) const {
    if (l == 1) return n;
    return sum(n, t_inner_, [&](std::size_t i) { return iteration(I, n, i, l); });
  }

  // One parfor iteration: v = I[i]; I' = INTERSECT(I, N+(v)); recurse on I'.
  // Every out-neighbour of v ranks above v and I is sorted, so only
  // I[i+1 ..] can meet N+(v); the intersection is the same either way.
  count_t iteration(const Id* I, std::size_t n, std::size_t i, std::size_t l) const {
    const std::size_t v = static_cast<std::size_t>(I[i]);
    const Id* a = I + i + 1;
    const std::size_t na = n - i - 1;
    const vid* b = dg_.neighbors(v);
    const std::size_t nb = dg_.degree(v);

    // Two serial passes, so that I' is allocated at exactly its size and no
    // uncharged scratch is needed: count, then fill.
    std::size_t size = 0;
    merge(a, na, b, nb, [&](Id) { size++; });
    if (l == 2 && c_.early_base) return size;
    if (c_.prune && size < l - 1) return 0;

    // An empty I' still allocates, as sequence always stores its capacity
    // word; so does an empty array in splang, which carries a size header.
    auto next = id_seq::uninitialized(size);
    std::size_t j = 0;
    merge(a, na, b, nb, [&](Id x) { next[j++] = x; });
    return rec(next.data(), size, l - 1);
    // next is freed here, after the recursion below it has joined.
  }

  // Calls out(x) for every x in both sorted lists, in order.
  template <typename Out>
  static void merge(const Id* a, std::size_t na, const vid* b, std::size_t nb, Out&& out) {
    std::size_t i = 0, j = 0;
    while (i < na && j < nb) {
      const std::uint64_t x = static_cast<std::uint64_t>(a[i]);
      const std::uint64_t y = b[j];
      if (x < y) i++;
      else if (y < x) j++;
      else {
        out(a[i]);
        i++, j++;
      }
    }
  }

  // --variant induced: GBBS's -space 6 --saveSpace (induced_split.h, with
  // HybridSpace_lw from intersect.h and KCliqueDir_fast_hybrid_rec from
  // induced_hybrid.h at recursive_level 0). Each top-level vertex v allocates
  // a workspace sized by its out-degree d, builds the subgraph induced on
  // N+(v) in it, and runs the rest of the recursion serially inside it,
  // allocating nothing more. GBBS's parallel_for and reduce in the setup are
  // serial loops here, so nothing below the top level forks.
  struct workspace {
    std::size_t d;
    Id* induced;      // the I of recursion level j, at [j*d, j*d + num_induced[j])
    Id* degs;         // degs[x]: the out-degree of local vertex x within N+(v)
    label_t* labels;  // labels[x]: the deepest level whose I holds x
    Id* edges;        // the out-neighbours of x within N+(v), at [x*d, x*d + degs[x])
    Id* num_induced;  // |I| at each level
  };

  count_t induced_vertex(std::size_t v, std::size_t d) const {
    // HybridSpace_lw::alloc(d, k-1, ...): GBBS's five mallocs, in its order,
    // each a space_sequence with its own capacity word. The level stack has
    // k-1 levels, as GBBS allocates it, though the recursion writes at most
    // k-2 of them. Under --prune off a vertex with d = 0 still allocates, as
    // its I' does in the faithful variant.
    auto induced = id_seq::uninitialized((k_ - 1) * d);
    auto degs = id_seq::uninitialized(d);
    auto labels = label_seq::uninitialized(d);
    auto edges = id_seq::uninitialized(d * d);
    auto num_induced = id_seq::uninitialized(k_ - 1);
    for (std::size_t x = 0; x < d; x++) labels[x] = 0;

    // HybridSpace_lw::setup_intersect: relabel N+(v) as 0 .. d-1, and build
    // row x of the induced subgraph by merging N+(v) with N+(N+(v)[x]).
    const vid* nv = dg_.neighbors(v);
    for (std::size_t x = 0; x < d; x++) {
      const vid* nu = dg_.neighbors(nv[x]);
      const std::size_t du = dg_.degree(nv[x]);
      std::size_t i = 0, j = 0, e = 0;
      while (i < d && j < du) {
        if (nv[i] < nu[j]) i++;
        else if (nu[j] < nv[i]) j++;
        else {
          edges[x * d + e++] = static_cast<Id>(i);
          i++, j++;
        }
      }
      degs[x] = static_cast<Id>(e);
    }
    for (std::size_t x = 0; x < d; x++) induced[x] = static_cast<Id>(x);
    num_induced[0] = static_cast<Id>(d);

    const workspace w{d, induced.data(), degs.data(), labels.data(), edges.data(),
                      num_induced.data()};
    return induced_rec(w, 1, k_ - 1);
    // The workspace is freed here, after the recursion.
  }

  // REC-COUNT-CLIQUES(DG, I, l) inside a workspace, serially, where I is level
  // lvl-1 of the stack. Its members are labelled lvl while it is being
  // processed, so I n N+(x) is row x of the induced subgraph filtered by
  // label. --early-base and --prune act as in the faithful variant; GBBS
  // always does both. The sum goes into a local, so there is no T to
  // materialize, and --T inner is the same as --T none.
  count_t induced_rec(const workspace& w, std::size_t lvl, std::size_t l) const {
    const std::size_t n = static_cast<std::size_t>(w.num_induced[lvl - 1]);
    if (l == 1) return n;
    const Id* I = w.induced + w.d * (lvl - 1);
    auto in_I = [&](Id x) { return static_cast<std::size_t>(w.labels[x]) == lvl; };
    for (std::size_t i = 0; i < n; i++) w.labels[I[i]] = static_cast<label_t>(lvl);

    count_t total = 0;
    for (std::size_t i = 0; i < n; i++) {
      const Id* row = w.edges + w.d * static_cast<std::size_t>(I[i]);
      const std::size_t deg = static_cast<std::size_t>(w.degs[I[i]]);
      if (l == 2 && c_.early_base) {
        for (std::size_t j = 0; j < deg; j++) total += in_I(row[j]);
        continue;
      }
      Id* next = w.induced + w.d * lvl;
      std::size_t size = 0;
      for (std::size_t j = 0; j < deg; j++) {
        if (in_I(row[j])) next[size++] = row[j];
      }
      w.num_induced[lvl] = static_cast<Id>(size);
      if (c_.prune && size < l - 1) continue;
      total += induced_rec(w, lvl + 1, l - 1);
    }

    for (std::size_t i = 0; i < n; i++) w.labels[I[i]] = static_cast<label_t>(lvl - 1);
    return total;
  }

  const graph_view dg_;
  const config& c_;
  const forks fk_;
  const std::size_t k_;
  const bool induced_;
  // Whether T is materialized at the top level, and below it (--T).
  const bool t_top_, t_inner_;
};

// ---------------------------------------------------------------------------

std::string json_string(const std::string& s) {
  std::string out = "\"";
  for (char ch : s) {
    if (ch == '"' || ch == '\\') out += '\\';
    out += ch;
  }
  return out + "\"";
}

}  // namespace

int main(int argc, char** argv) {
  const config c = parse(argc, argv);

  splang_bench::timer prep;
  std::string graph_name;
  csr g;
  if (c.rmat) {
    g = rmat(c.rmat_n, c.rmat_m, c.rmat_seed);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "rmat:%llu,%llu,%llu",
                  static_cast<unsigned long long>(c.rmat_n),
                  static_cast<unsigned long long>(c.rmat_m),
                  static_cast<unsigned long long>(c.rmat_seed));
    graph_name = buf;
  }
  else {
    g = read_adjacency_graph(c.graph_file);
    graph_name = c.graph_file;
  }
  if (!c.write_graph.empty()) write_adjacency_graph(g, c.write_graph);
  std::size_t degeneracy = 0;
  const bool kcore = c.order == "kcore";
  const csr dg = orient(g, c.k, kcore ? kcore_rank(g, degeneracy) : degree_rank(g));
  std::size_t max_out = 0;
  for (std::size_t v = 0; v < dg.n; v++) max_out = std::max(max_out, dg.degree(v));
  const double prep_ms = prep.ms();

  count_t value = 0;
  // The exact footprint serializes every charge and credit on one counter,
  // which dominates the time at high P, so timing runs turn it off. R1, R* and
  // Rinf come from the vertex and are the same either way.
  parlay::space_track_footprint(c.footprint);
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto count = [&](const graph_view& view) {
    return c.id64 ? arb_count<std::int64_t>(view, c).run() : arb_count<vid>(view, c).run();
  };
  auto vtx = parlay::augment(parlay::space_vertex{}, [&]() {
    if (!c.charge_graph) {
      value = count(graph_view{dg.n, dg.off.data(), dg.adj.data()});
      return;
    }
    // --charge-graph: the computation allocates its own copy of the oriented
    // graph, counts on it, and frees it, so the graph is measured: it is live
    // across the whole count and lands in S. The copies are serial so that
    // they add no forks.
    auto off = parlay::space_sequence<std::uint64_t>::uninitialized(dg.n + 1);
    std::copy(dg.off.begin(), dg.off.end(), off.begin());
    auto adj = parlay::space_sequence<vid>::uninitialized(dg.adj.size());
    std::copy(dg.adj.begin(), dg.adj.end(), adj.begin());
    value = count(graph_view{dg.n, off.data(), adj.data()});
  });
  const double ms = t.ms();

  // Every vertex allocates its top-level I' unless pruning skips them all;
  // --T all and --charge-graph allocate regardless.
  const bool expect_alloc =
      dg.n > 0 && (c.t_mode == "all" || c.charge_graph || !c.prune ||
                   max_out >= static_cast<std::size_t>(c.k - 1));
  const splang_bench::measures m = splang_bench::collect(vtx, expect_alloc);

  const char* id = c.id64 ? "i64" : "u32";
  const char* join = c.join_wait ? "wait" : "steal";
  // The degeneracy comes out of the k-core peel, so it is only known (and
  // null otherwise) under --order kcore.
  const std::string degeneracy_str = kcore ? std::to_string(degeneracy) : "null";
  if (c.json) {
    std::printf("{\"example\":\"par-clique\",\"graph\":%s,\"n\":%zu,\"m\":%zu,"
                "\"m_oriented\":%zu,\"max_out_degree\":%zu,\"degeneracy\":%s,\"k\":%ld,"
                "\"variant\":\"%s\",\"T\":\"%s\",\"order\":\"%s\",\"early_base\":%s,"
                "\"prune\":%s,\"id\":\"%s\",\"grain\":%zu,\"join\":\"%s\",\"charge_graph\":%s,"
                "\"footprint_tracked\":%s,"
                "\"threads\":%lld,\"value\":\"%llu\",\"prep_ms\":%.3f,",
                json_string(graph_name).c_str(), g.n, g.adj.size() / 2, dg.adj.size(),
                max_out, degeneracy_str.c_str(), c.k, c.variant.c_str(), c.t_mode.c_str(),
                c.order.c_str(), c.early_base ? "true" : "false", c.prune ? "true" : "false",
                id, c.grain, join, c.charge_graph ? "true" : "false",
                c.footprint ? "true" : "false", m.threads,
                static_cast<unsigned long long>(value), prep_ms);
    splang_bench::print_measures_json(m, ms);
    std::printf("}\n");
  }
  else {
    std::printf("graph %s: n %zu, m %zu, oriented m %zu, max out-degree %zu",
                graph_name.c_str(), g.n, g.adj.size() / 2, dg.adj.size(), max_out);
    if (kcore) std::printf(", degeneracy %zu", degeneracy);
    std::printf("\n");
    std::printf("k %ld, variant %s, T %s, order %s, early-base %s, prune %s, id %s, grain %zu, "
                "join %s, charge-graph %s\n",
                c.k, c.variant.c_str(), c.t_mode.c_str(), c.order.c_str(),
                c.early_base ? "on" : "off", c.prune ? "on" : "off", id, c.grain, join,
                c.charge_graph ? "on" : "off");
    std::printf("threads %lld\n", m.threads);
    std::printf("%-22s %llu\n", "value", static_cast<unsigned long long>(value));
    splang_bench::print_measures_text(m, ms);
    std::printf("%-22s %.0fms\n", "prep", prep_ms);
  }
  return 0;
}
