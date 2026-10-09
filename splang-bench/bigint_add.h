// Big integer addition and subtraction, ported from examples/bigint_add.h with
// every bigint it allocates charged to the space_vertex of the strand that
// allocates it.
//
// Each integer is a sequence of unsigned digits, least significant first. The
// carry is propagated serially below 65536 digits and with a scan above it.
//
// The example reads the integers as two's complement, with the top bit of the
// last digit as the sign. Karatsuba only ever handles non-negative values, but
// it feeds add the low halves of its operands, whose top digit is arbitrary,
// so the example sign-extends some of them as negative and gets wrong
// products. Its parallel path also adds the scanned carry state itself, 2 for
// propagate, and leaves out the extra one. Here every integer is unsigned: the
// shorter operand is extended with zeros, a carry out of the top digit is
// appended as a new digit, and subtract requires a >= b and drops the carry
// out. The allocations are the example's, one result per call plus a
// push_back when the sum grows.
//
// The results are built with the space allocator rather than
// parlay::to_sequence, whose default allocator would go uncounted. The
// parallel path's scan also keeps a short sequence of block sums internally,
// which is not charged.

#ifndef SPLANG_BENCH_BIGINT_ADD_H_
#define SPLANG_BENCH_BIGINT_ADD_H_

#include <algorithm>

#include <parlay/delayed.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>

// use 128 bit integers if available
#ifdef __SIZEOF_INT128__
using digit = unsigned long;
using double_digit = unsigned __int128;
#else
using digit = unsigned int;
using double_digit = unsigned long;
#endif

constexpr int digit_len = sizeof(digit) * 8;

// Allocations are reported to the current strand's vertex. Outside an
// augmented region this is an ordinary parlay::sequence.
using bigint = parlay::space_sequence<digit>;

namespace bigint_detail {

// a + b, or a - b if subtract, over max(na, nb) digits
template <typename Bigint1, typename Bigint2>
bigint add_or_subtract(const Bigint1& a, const Bigint2& b, bool subtract) {
  long na = a.size();  long nb = b.size();
  long n = std::max(na, nb);
  enum carry : char { no = 0, yes = 1, propagate = 2 };
  double_digit mask = (static_cast<double_digit>(1) << digit_len) - 1;

  // Both operands are extended with zeros to n digits, and b is negated
  // (inverted here, plus the one added in at digit 0) for a subtraction.
  auto A = [&](long i) { return (i < na) ? static_cast<digit>(a[i]) : digit{0}; };
  auto B = [&](long i) {
    digit d = (i < nb) ? static_cast<digit>(b[i]) : digit{0};
    return subtract ? static_cast<digit>(~d) : d;
  };

  bigint result;
  bool carry_out;
  if (n < 65536) {  // if not large do sequentially
    double_digit carry = subtract;
    result = bigint::uninitialized(n);
    for (long i = 0; i < n; i++) {
      double_digit s = A(i) + (B(i) + carry);
      result[i] = s & mask;
      carry = s >> digit_len;
    }
    carry_out = carry;
  } else {  // do in parallel
    // check which digits will carry or propagate
    auto c = parlay::delayed::tabulate(n, [&](long i) {
      double_digit s = A(i) + static_cast<double_digit>(B(i));
      s += (i == 0 && subtract);
      return static_cast<carry>(2 * (s == mask) + (s >> digit_len));
    });

    // use scan to do the propagation
    auto f = [](carry a, carry b) { return (b == propagate) ? a : b; };
    auto [cc, total] = parlay::delayed::scan(c, parlay::binary_op(f, propagate));
    auto z = parlay::delayed::zip(cc, parlay::iota(n));
    result = parlay::delayed::to_sequence<digit, parlay::space_allocator<digit>>(
        parlay::delayed::map(z, [&](auto p) {
          // A digit's carry in is the extra one at digit 0, and otherwise
          // yes or, if every digit below it propagates, none.
          auto [ci, i] = p;
          return static_cast<digit>(A(i) + B(i) + (ci == yes) + (i == 0 && subtract));
        }));
    carry_out = (total == yes);
  }
  // A subtraction's carry out is the borrow cancelling, since a >= b.
  if (!subtract && carry_out) result.push_back(1);
  return result;
}

}  // namespace bigint_detail

// Templatizing allows using delayed sequences and slices.
template <typename Bigint1, typename Bigint2>
bigint add(const Bigint1& a, const Bigint2& b) {
  return bigint_detail::add_or_subtract(a, b, false);
}

// a - b, for a >= b
template <typename Bigint1, typename Bigint2>
bigint subtract(const Bigint1& a, const Bigint2& b) {
  return bigint_detail::add_or_subtract(a, b, true);
}

#endif  // SPLANG_BENCH_BIGINT_ADD_H_
