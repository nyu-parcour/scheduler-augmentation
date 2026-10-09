// MergeFeyn quantum circuit simulation, measured with scheduler augmentation.
//
// A port of ../mergefeyn, the driver in src/main.cpp run as
// run_sweep.sh runs it; see mergefeyn.h for the recursion. --size is the
// number of qubits N of a W-state circuit (scripts/gen_wstate.py), which is
// simulated from |0...0> with one MergeFeyn call at the driver's default
// grain of 1000. The circuit has 2(N-1) branching gates, so the search tree
// has up to 2^(2(N-1)) leaves, but most paths cancel: the final state has
// only N entries.
//
// Every leaf of the search tree allocates a one-entry state, and every merge
// allocates a state holding both of its inputs, which stay live until the
// merge returns. The circuit is built outside the augmented region and is
// never charged; it is read-only input, as the index is in ann.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <parlay/parallel.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "mergefeyn.h"
#include "report.h"

namespace {

using namespace mergefeyn;

// scripts/gen_wstate.py: the W state in the same layout as QASMBench's
// wstate_nN.
std::vector<Gate> wstate(int n) {
  std::vector<Gate> g{Gate::x(n - 1)};
  for (int k = n - 2; k >= 0; k--) {
    double t = std::acos(std::sqrt(1.0 / (k + 2)));
    g.push_back(Gate::ry(k, -t));
    g.push_back(Gate::cz(k + 1, k));
    g.push_back(Gate::ry(k, t));
  }
  for (int k = n - 2; k >= 0; k--) g.push_back(Gate::cx(k, k + 1));
  return g;
}

}  // namespace

int main(int argc, char** argv) {
  const auto opts = splang_bench::parse_args(argc, argv, 15);
  const int n = static_cast<int>(opts.size);
  if (n < 2 || n > 64) {
    std::fprintf(stderr, "--size must be between 2 and 64 qubits\n");
    return 2;
  }
  constexpr std::int64_t grain = 1000;

  const std::vector<Gate> gates = wstate(n);
  const WeightedIdx initial[] = {{0, {1, 0}}};

  MergeFeynResult result;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() {
    result = MergeFeyn(gates, initial, 1, grain);
  });
  const double ms = t.ms();

  // The W state: amplitude 1/sqrt(N) on each basis state with exactly one
  // qubit set, up to the 1e-8 path cutoff.
  const auto& e = result.state.entries();
  bool correct = static_cast<int>(e.size()) == n;
  for (const auto& w : e) {
    correct = correct && w.idx != 0 && (w.idx & (w.idx - 1)) == 0 &&
              std::abs(std::hypot(w.weight.re, w.weight.im) - 1 / std::sqrt(n)) < 1e-4;
  }

  char value[64];
  std::snprintf(value, sizeof(value), "%s(%zu nonzero)", correct ? "" : "WRONG ", e.size());
  splang_bench::report("mergefeyn", opts, value, v, ms);
  if (!correct) std::fprintf(stderr, "not a W state: %zu nonzero\n", e.size());
  return correct ? 0 : 1;
}
