// A port of splang's `allocfree` (Splang/Examples/Demo.lean:115-123):
//
//   let parfor := fn rec parfor p =>
//     let n := fst p in let f := snd p in
//     if n <= 1 then f ()
//     else let h := n / 2 in parfor (h, f) ||| parfor (n - h, f); ()
//   in
//   parfor (n, fn _ => let b := alloc 1000 0 in free b)
//
// n leaves, each of which allocates a 1000-cell array and immediately frees
// it. Nothing is live across a fork, so one processor holds one array at a
// time while an unbounded number hold all n: R1 = 1000, Rinf = 1000n.

#include <cstdint>

#include <parlay/parallel.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"

namespace {

// splang: alloc 1000 0
constexpr std::int64_t leaf_cells = 1000;

void leaf() {
  std::int64_t* b = parlay::space_alloc<std::int64_t>(leaf_cells, 0);
  splang_bench::do_not_optimize(b);
  parlay::space_free(b, leaf_cells);
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
