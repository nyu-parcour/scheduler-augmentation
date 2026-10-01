// Strassen matrix multiply, measured with scheduler augmentation.
//
// splang's `strassenDemo` (Splang/Examples/Strassen.lean) follows this
// implementation allocation for allocation, so compare.py checks the two
// against each other like the other ports. The operand values differ (random
// doubles here, fixed integer matrices there), which the space measures do not
// see. What makes it interesting is the shape of the recursion. Each level
// copies out eight quadrant matrices and holds them across a seven-way fork,
// so R-infinity grows by 7/4 per level while R1 only pays for one root-to-leaf
// path.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"
#include "strassen.h"

namespace {

// P = A x V for an m x n matrix A with row width rw (sequential)
void mat_vec_mul(long m, long n, long rw, const REAL* A, const REAL* V, REAL* P) {
  for (long i = 0; i < m; i++) {
    REAL c = 0;
    for (long j = 0; j < n; j++) c += A[i * rw + j] * V[j];
    P[i] = c;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto opts = splang_bench::parse_args(argc, argv, 1024);
  const long n = static_cast<long>(opts.size);
  if (n <= 0 || (n & (n - 1)) != 0 || (n % strassen_base) != 0) {
    std::cerr << "--size must be a power of 2 and a multiple of " << strassen_base << "\n";
    return 2;
  }

  // The operand values are generated outside the augmented region, and kept
  // there for the check below.
  parlay::random_generator gen(0);
  std::uniform_real_distribution<REAL> dis(0.0, 1.0);
  auto A = parlay::tabulate(n * n, [&](long i) { auto r = gen[i]; return dis(r); });
  auto B = parlay::tabulate(n * n, [&](long i) { auto r = gen[n * n + i]; return dis(r); });

  // As in splang's strassenDemo, the computation allocates its own copies of
  // the operands, multiplies, and frees them, so the inputs are measured: they
  // are live across the whole recursion and land in S. The copies are serial
  // so that they add no forks.
  matrix C;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() {
    matrix a = matrix::uninitialized(n * n);
    std::copy(A.begin(), A.end(), a.begin());
    matrix b = matrix::uninitialized(n * n);
    std::copy(B.begin(), B.end(), b.begin());
    C = strassen(a, b, n);
  });
  const double ms = t.ms();

  // Freivalds-style check: compare A (B r) with C r for a random vector r.
  auto r = parlay::tabulate(n, [&](long i) { auto g = gen[2 * n * n + i]; return dis(g); });
  parlay::sequence<REAL> Br(n), ABr(n), Cr(n);
  mat_vec_mul(n, n, n, B.data(), r.data(), Br.data());
  mat_vec_mul(n, n, n, A.data(), Br.data(), ABr.data());
  mat_vec_mul(n, n, n, C.data(), r.data(), Cr.data());
  REAL max_rel_err = 0.0;
  for (long i = 0; i < n; i++)
    max_rel_err = std::max(max_rel_err, std::abs(ABr[i] - Cr[i]) / std::abs(ABr[i]));
  const bool correct = max_rel_err < 1e-6;

  char value[64];
  std::snprintf(value, sizeof(value), "%s(err %.2g)", correct ? "" : "WRONG ", max_rel_err);
  splang_bench::report("strassen", opts, value, v, ms);
  return correct ? 0 : 1;
}
