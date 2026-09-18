// Strassen matrix multiply, with every matrix it allocates charged to the
// space_vertex of the strand that allocates it.
//
// Matrices are n x n, row-major and contiguous. The recursion splits each
// operand into four quadrants, which means copying them out into fresh
// matrices rather than working in place on strides. Those copies are the
// point: the eight quadrants of a node stay live across the seven-way fork
// below it, so the memory a fully parallel execution needs grows by 7/4 per
// level while a depth-first one only pays for a single root-to-leaf path.
//
// The seven subproducts are arranged as a binary fork tree, since par_do is
// binary:
//
//   ((M1 | M2) | (M3 | M4)) | ((M5 | M6) | M7)

#ifndef SPLANG_BENCH_STRASSEN_H_
#define SPLANG_BENCH_STRASSEN_H_

#include <parlay/parallel.h>
#include <parlay/sequence.h>
#include <parlay/vertices/space_alloc.h>

using REAL = double;

// Allocations are reported to the current strand's vertex. Outside an
// augmented region this is an ordinary parlay::sequence.
using matrix = parlay::space_sequence<REAL>;

// The recursion multiplies directly at this size. The driver requires n to be
// a power of two and a multiple of 16, so the halving lands on it exactly.
constexpr long strassen_base = 16;

namespace strassen_detail {

// dst[h*h] <- quadrant (qi, qj) of the n x n matrix src
inline void quadrant(REAL* dst, const REAL* src, long n, long qi, long qj) {
  const long h = n / 2;
  for (long i = 0; i < h; i++)
    for (long j = 0; j < h; j++) dst[i * h + j] = src[(qi * h + i) * n + qj * h + j];
}

// dst <- x + s*y over m elements, with s either +1 or -1
inline void combine(REAL* dst, const REAL* x, const REAL* y, long m, REAL s) {
  for (long i = 0; i < m; i++) dst[i] = x[i] + s * y[i];
}

// c <- a * b, the n x n base case
inline void multiply_base(REAL* c, const REAL* a, const REAL* b, long n) {
  for (long i = 0; i < n; i++)
    for (long j = 0; j < n; j++) {
      REAL sum = 0;
      for (long k = 0; k < n; k++) sum += a[i * n + k] * b[k * n + j];
      c[i * n + j] = sum;
    }
}

matrix multiply(const REAL* A, const REAL* B, long n);

// One subproduct: (x1 +sx x2) * (y1 +sy y2), where a null second operand means
// the first is used directly and no temporary is allocated for that side.
inline matrix subproduct(long hh, long h,
                         const REAL* x1, const REAL* x2, REAL sx,
                         const REAL* y1, const REAL* y2, REAL sy) {
  // The operand temporaries live until this function returns, that is until
  // after the recursive call below has finished, so they are held across every
  // fork that call performs.
  matrix s, t;
  const REAL* left = x1;
  const REAL* right = y1;
  if (x2 != nullptr) {
    s = matrix::uninitialized(hh);
    combine(s.data(), x1, x2, hh, sx);
    left = s.data();
  }
  if (y2 != nullptr) {
    t = matrix::uninitialized(hh);
    combine(t.data(), y1, y2, hh, sy);
    right = t.data();
  }
  return multiply(left, right, h);
}

inline matrix multiply(const REAL* A, const REAL* B, long n) {
  matrix C = matrix::uninitialized(n * n);
  if (n <= strassen_base) {
    multiply_base(C.data(), A, B, n);
    return C;
  }

  const long h = n / 2;
  const long hh = h * h;

  matrix a11 = matrix::uninitialized(hh), a12 = matrix::uninitialized(hh);
  matrix a21 = matrix::uninitialized(hh), a22 = matrix::uninitialized(hh);
  matrix b11 = matrix::uninitialized(hh), b12 = matrix::uninitialized(hh);
  matrix b21 = matrix::uninitialized(hh), b22 = matrix::uninitialized(hh);
  quadrant(a11.data(), A, n, 0, 0);
  quadrant(a12.data(), A, n, 0, 1);
  quadrant(a21.data(), A, n, 1, 0);
  quadrant(a22.data(), A, n, 1, 1);
  quadrant(b11.data(), B, n, 0, 0);
  quadrant(b12.data(), B, n, 0, 1);
  quadrant(b21.data(), B, n, 1, 0);
  quadrant(b22.data(), B, n, 1, 1);

  matrix m1, m2, m3, m4, m5, m6, m7;

  auto p1 = [&] { m1 = subproduct(hh, h, a11.data(), a22.data(), +1,
                                          b11.data(), b22.data(), +1); };
  auto p2 = [&] { m2 = subproduct(hh, h, a21.data(), a22.data(), +1,
                                          b11.data(), nullptr,   +1); };
  auto p3 = [&] { m3 = subproduct(hh, h, a11.data(), nullptr,    +1,
                                          b12.data(), b22.data(), -1); };
  auto p4 = [&] { m4 = subproduct(hh, h, a22.data(), nullptr,    +1,
                                          b21.data(), b11.data(), -1); };
  auto p5 = [&] { m5 = subproduct(hh, h, a11.data(), a12.data(), +1,
                                          b22.data(), nullptr,   +1); };
  auto p6 = [&] { m6 = subproduct(hh, h, a21.data(), a11.data(), -1,
                                          b11.data(), b12.data(), +1); };
  auto p7 = [&] { m7 = subproduct(hh, h, a12.data(), a22.data(), -1,
                                          b21.data(), b22.data(), +1); };

  parlay::par_do([&] { parlay::par_do([&] { parlay::par_do(p1, p2); },
                                      [&] { parlay::par_do(p3, p4); }); },
                 [&] { parlay::par_do([&] { parlay::par_do(p5, p6); }, p7); });

  // C11 = M1+M4-M5+M7   C12 = M3+M5
  // C21 = M2+M4         C22 = M1-M2+M3+M6
  REAL* c = C.data();
  for (long i = 0; i < h; i++)
    for (long j = 0; j < h; j++) {
      const long k = i * h + j;
      c[i * n + j] = m1[k] + m4[k] - m5[k] + m7[k];
      c[i * n + h + j] = m3[k] + m5[k];
      c[(h + i) * n + j] = m2[k] + m4[k];
      c[(h + i) * n + h + j] = m1[k] - m2[k] + m3[k] + m6[k];
    }

  return C;
}

}  // namespace strassen_detail

// C = A * B for n x n row-major matrices. The returned matrix is the only
// allocation that outlives the call, so it is the computation's net growth.
template <typename Seq>
inline matrix strassen(const Seq& A, const Seq& B, long n) {
  return strassen_detail::multiply(A.data(), B.data(), n);
}

#endif  // SPLANG_BENCH_STRASSEN_H_
