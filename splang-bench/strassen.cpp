// Strassen matrix multiply, measured with scheduler augmentation.
//
// Unlike allocfree and nqueens this is not a port of a splang program, so
// there is no reference to compare against: it is an ordinary ParlayLib
// computation whose space is being measured. What makes it interesting is the
// shape of the recursion. Each level copies out eight quadrant matrices and
// holds them across a seven-way fork, so R-infinity grows by 7/4 per level
// while R1 only pays for one root-to-leaf path.

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

  // Built outside the augmented region: the inputs are given, not allocated by
  // the computation, so they are not part of what is being measured.
  parlay::random_generator gen(0);
  std::uniform_real_distribution<REAL> dis(0.0, 1.0);
  auto A = parlay::tabulate(n * n, [&](long i) { auto r = gen[i]; return dis(r); });
  auto B = parlay::tabulate(n * n, [&](long i) { auto r = gen[n * n + i]; return dis(r); });

  matrix C;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() { C = strassen(A, B, n); });
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
