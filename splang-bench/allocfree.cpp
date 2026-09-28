// A port of splang's `allocfree` (Splang/Examples/AllocFree.lean):
//
//   let parfor := fn rec parfor p =>
//     let n := fst p in let f := snd p in
//     if n <= 1 then f ()
//     else let h := n / 2 in parfor (h, f) ||| parfor (n - h, f); ()
//   in
//   parfor (n, fn _ => let b := alloc 1000 0 in free b)
//
// n leaves, each of which allocates a 1000-element array and immediately frees
// it. splang charges an array one header cell for its length on top of its
// elements, so each array costs 1001 cells. Nothing is live across a fork, so
// one processor holds one array at a time while an unbounded number hold all
// n: R1 = 1001, Rinf = 1001n.

#include <cstdint>

#include <parlay/parallel.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"

namespace {

// splang: alloc 1000 0
constexpr std::int64_t leaf_elements = 1000;

// The array is a parlay::sequence, charged for everything it allocates. That
// includes the capacity word sequence prepends to each buffer, which plays the
// part of splang's header cell, so an array costs 1001 cells as it does in
// splang. 1000 elements is below parallel_for's granularity threshold, so the
// initialization runs serially and adds no forks.
void leaf() {
  parlay::space_sequence<std::int64_t> b(leaf_elements, 0);
  splang_bench::do_not_optimize(b.data());
}

// The recursion has no grain cutoff, because splang's has none: a serial
// cutoff would collapse interior forks and measure a different graph.
void parfor_tree(std::int64_t n) {
  if (n <= 1) {
    leaf();
    return;
  }
  const std::int64_t h = n / 2;
  parlay::par_do([=]() { parfor_tree(h); },
                 [=]() { parfor_tree(n - h); });
}

}  // namespace

int main(int argc, char** argv) {
  const auto opts = splang_bench::parse_args(argc, argv, 3000);

  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() { parfor_tree(opts.size); });
  const double ms = t.ms();

  splang_bench::report("allocfree", opts, "()", v, ms);
  return 0;
}
