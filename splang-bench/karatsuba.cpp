// Karatsuba multiplication of two big integers, measured with scheduler
// augmentation.
//
// A port of examples/karatsuba.cpp; see karatsuba.h for the recursion and why
// its memory grows with parallelism. There is no splang counterpart, so
// compare.py does not check this one. The operands are random, --size digits
// each, with the top bit of each clear so that they are non-negative.

#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

#include "karatsuba.h"
#include "report.h"

namespace {

// x mod 2^61-1, where x is read as an unsigned integer (most significant
// digit last).
template <typename Bigint>
std::uint64_t mod_mersenne61(const Bigint& x) {
  constexpr std::uint64_t p = (std::uint64_t{1} << 61) - 1;
  // 2^64 = 8 (mod p)
  std::uint64_t r = 0;
  for (long i = static_cast<long>(x.size()) - 1; i >= 0; i--) {
    unsigned __int128 t = static_cast<unsigned __int128>(r) * 8 % p + x[i] % p;
    r = static_cast<std::uint64_t>(t % p);
  }
  return r;
}

std::uint64_t mulmod61(std::uint64_t a, std::uint64_t b) {
  constexpr std::uint64_t p = (std::uint64_t{1} << 61) - 1;
  return static_cast<std::uint64_t>(static_cast<unsigned __int128>(a) * b % p);
}

}  // namespace

int main(int argc, char** argv) {
  const auto opts = splang_bench::parse_args(argc, argv, 4096);
  const long m = static_cast<long>(opts.size);
  if (m <= 0) {
    std::fprintf(stderr, "--size must be positive\n");
    return 2;
  }

  // The operand values are generated outside the augmented region, and kept
  // there for the check below.
  auto randnum = [](long m, long seed) {
    parlay::random_generator gen(seed);
    std::uniform_int_distribution<digit> dis(0, std::numeric_limits<digit>::max());
    return parlay::tabulate(m, [&](long i) {
      auto r = gen[i];
      if (i == m - 1) return static_cast<digit>(dis(r) / 2);  // to ensure it is not negative
      else return dis(r);
    });
  };
  auto A = randnum(m, 0);
  auto B = randnum(m, 1);

  // As in strassen, the computation allocates its own copies of the operands,
  // so the inputs are measured: they are live across the whole recursion and
  // land in S. The copies are serial so that they add no forks.
  bigint C;
  parlay::space_reset_counters();
  splang_bench::timer t;
  auto v = parlay::augment(parlay::space_vertex{}, [&]() {
    bigint a = bigint::uninitialized(m);
    std::copy(A.begin(), A.end(), a.begin());
    bigint b = bigint::uninitialized(m);
    std::copy(B.begin(), B.end(), b.begin());
    C = karatsuba(a, b);
  });
  const double ms = t.ms();

  // Check the product modulo the prime 2^61-1.
  const bool correct = mod_mersenne61(C) == mulmod61(mod_mersenne61(A), mod_mersenne61(B));

  splang_bench::report("karatsuba", opts, correct ? "" : "WRONG", v, ms);
  return correct ? 0 : 1;
}
