// A port of splang's `nqueens` (Splang/Examples/NQueens.lean:10-68).
//
// Counts the ways to place n non-attacking queens. What is forked is the
// *column range* for the current row: [lo, hi) splits in half and the two
// halves are explored in parallel. At a single column the move is played into
// a freshly allocated copy of the board, which then stays live for the whole
// recursive subtree below it, and is freed once that subtree finishes.
//
// That board is the reason R1 and Rinf separate so far. One processor holds
// one board per row of a single root-to-leaf path (R1 = n(n+1)), while an
// unbounded number hold one per surviving branch of the whole search tree
// (Rinf = 16456 at n = 8).

#include <cstdint>
#include <cstdio>

#include <parlay/parallel.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "report.h"

namespace {

// The board is a parlay::sequence, charged for everything it allocates. That
// includes the capacity word sequence prepends to each buffer, so a board
// costs n+1 cells where splang's costs n. At these sizes the element
// initialization stays below parallel_for's granularity threshold and so runs
// serially, adding no forks.
using board = parlay::space_sequence<std::int64_t>;

// Does a queen at row k, column c attack any of the first k rows?
bool safe(const std::int64_t* brd, std::int64_t k, std::int64_t c) {
  for (std::int64_t i = 0; i < k; i++) {
    const std::int64_t q = brd[i];
    if (q == c) return false;
    if (q - i == c - k) return false;
    if (q + i == c + k) return false;
  }
  return true;
}

// Rows 0..k-1 are placed in brd; count the solutions that put row k's queen
// in some column of [lo, hi). brd is shared and read-only: both branches
// read it, neither writes it.
std::int64_t nq(std::int64_t n, const std::int64_t* brd,
                std::int64_t k, std::int64_t lo, std::int64_t hi) {
  if (hi - lo <= 0) return 0;

  if (hi - lo == 1) {
    if (!safe(brd, k, lo)) return 0;
    board b2(n, 0);
    for (std::int64_t i = 0; i < n; i++) b2[i] = brd[i];
    b2[k] = lo;
    // b2 is destroyed at the end of this scope, that is after the recursion
    // has joined, so it stays live across every fork the subtree performs.
    return (k + 1 == n) ? 1 : nq(n, b2.data(), k + 1, 0, n);
  }

  const std::int64_t m = lo + (hi - lo) / 2;
  std::int64_t a = 0, b = 0;
  parlay::par_do([&]() { a = nq(n, brd, k, lo, m); },
                 [&]() { b = nq(n, brd, k, m, hi); });
  return a + b;
}

std::int64_t nqueens(std::int64_t n) {
  board b(n, 0);
  return nq(n, b.data(), 0, 0, n);
}

}  // namespace

int main(int argc, char** argv) {
  const auto opts = splang_bench::parse_args(argc, argv, 8);

  std::int64_t value = 0;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() { value = nqueens(opts.size); });
  const double ms = t.ms();

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
  splang_bench::report("nqueens", opts, buf, v, ms);
  return 0;
}
