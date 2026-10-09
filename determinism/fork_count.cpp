// Counts forks in the DAG via a vertex; run repeatedly to see run-to-run variance.
#include <parlay/parallel.h>
#include <parlay/sequence.h>
#include <parlay/primitives.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <utility>
struct fork_count {
  static constexpr bool enabled = true;
  long forks = 0;
  void start() {} void stop() {}
  void fork(fork_count*, fork_count*) {}
  void join(fork_count* l, fork_count* r, fork_count* j) { j->forks = forks + l->forks + r->forks + 1; }
};
template <class F> long count(F f) { return parlay::augment(fork_count{}, f).forks; }
using P = std::pair<uint32_t, float>;
int main(int argc, char** argv) {
  const char* which = argv[1];
  long n = argc > 2 ? atol(argv[2]) : 288;
  long c = 0;
  if (!strcmp(which, "pair_default")) c = count([&]{ parlay::sequence<P> s(n); });
  else if (!strcmp(which, "pair_fill")) c = count([&]{ parlay::sequence<P> s(n, P{1, 2.f}); });
  else if (!strcmp(which, "u64_default")) c = count([&]{ parlay::sequence<uint64_t> s(n); });
  else if (!strcmp(which, "u64_fill")) c = count([&]{ parlay::sequence<uint64_t> s(n, 7); });
  else if (!strcmp(which, "pair_tabulate")) c = count([&]{ auto s = parlay::tabulate(n, [](size_t i){ return P{(uint32_t)i, 1.f}; }); });
  else if (!strcmp(which, "u64_tabulate")) c = count([&]{ auto s = parlay::tabulate(n, [](size_t i){ return (uint64_t)i; }); });
  else if (!strcmp(which, "pfor_default")) { volatile long sink = 0; c = count([&]{ parlay::parallel_for(0, n, [&](size_t i){ sink += i; }); }); }
  else if (!strcmp(which, "pfor_gran1")) { volatile long sink = 0; c = count([&]{ parlay::parallel_for(0, n, [&](size_t i){ sink += i; }, 1); }); }
  printf("%ld\n", c);
}
