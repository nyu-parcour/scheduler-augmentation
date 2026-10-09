// Karatsuba multiplication of non-negative big integers, ported from
// examples/karatsuba.h with every bigint it allocates charged to the
// space_vertex of the strand that allocates it.
//
// Each level splits both operands at nhalf digits and forks three half-size
// products:
//
//   z0 = low_a * low_b   z1 = (low_a + high_a) * (low_b + high_b)   z2 = high_a * high_b
//
// The halves are slices, so they cost nothing, but z1's two operand sums are
// fresh bigints that stay live until z1's recursion returns, so they are held
// across every fork below it. Afterwards z0, z1 and z2 are all live while the
// level combines them. A fully parallel execution pays for this at every node
// of a level at once, 3/2 as many digits per level down, while a depth-first
// one only pays for a single root-to-leaf path.

#ifndef SPLANG_BENCH_KARATSUBA_H_
#define SPLANG_BENCH_KARATSUBA_H_

#include <algorithm>

#include <parlay/delayed.h>
#include <parlay/parallel.h>
#include <parlay/sequence.h>

#include "bigint_add.h"

// The recursion multiplies directly once the shorter operand is this long.
constexpr long karatsuba_base = 128;

// sequential n^2 version for small numbers. The example sums the digit
// products of each result digit in a double_digit, which overflows once a
// digit is as wide as half of one; here a is instead multiplied by one digit
// of b at a time, with each row's carry fitting in a digit.
template <typename Bigint>
bigint small_multiply(const Bigint& a, const Bigint& b) {
  long na = a.size();  long nb = b.size();
  if (na < nb) return small_multiply(b, a);
  double_digit mask = (static_cast<double_digit>(1) << digit_len) - 1;

  bigint result(na + nb, 0);
  for (long j = 0; j < nb; j++) {
    double_digit carry = 0;
    for (long i = 0; i < na; i++) {
      double_digit accum = static_cast<double_digit>(a[i]) * b[j] + result[i + j] + carry;
      result[i + j] = accum & mask;
      carry = accum >> digit_len;
    }
    result[j + na] = carry;
  }
  return result;
}

// shift a left by n digits (i.e multiply a by d^n).
inline auto shift(const bigint& a, long n) {
  return parlay::delayed::tabulate(a.size() + n, [&a, n](long i) {
    return (i < n) ? digit{0} : a[i - n];
  });
}

// borrowed from the wikipedia page
template <typename Bigint>
bigint karatsuba(const Bigint& a, const Bigint& b) {
  long na = a.size();  long nb = b.size();
  if (na < nb) return karatsuba(b, a);
  if (nb <= karatsuba_base) return small_multiply(a, b);
  long nhalf = nb / 2;
  auto low_a = a.cut(0, nhalf);  auto high_a = a.cut(nhalf, na);
  auto low_b = b.cut(0, nhalf);  auto high_b = b.cut(nhalf, nb);
  bigint z0, z1, z2;
  parlay::par_do3([&] { z0 = karatsuba(low_a, low_b); },
                  [&] { z1 = karatsuba(add(low_a, high_a), add(low_b, high_b)); },
                  [&] { z2 = karatsuba(high_a, high_b); });

  auto mid = subtract(z1, add(z0, z2));
  return add(shift(z2, 2 * nhalf), add(shift(mid, nhalf), z0));
}

#endif  // SPLANG_BENCH_KARATSUBA_H_
