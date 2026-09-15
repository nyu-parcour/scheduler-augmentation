// Minimal example of scheduler augmentation with a custom vertex:
// parallel fib instrumented with work and burdened-span analysis.
//
// Usage: ./fib [n] [burden_us]
//   n          fib argument               (default 42)
//   burden_us  span burden per join, us   (default 500)

#include <cstdio>
#include <cstdlib>

#include <chrono>

#include <parlay/parallel.h>

#include "burdened_vertex.h"

long long fib(long long n) {
  if (n < 20) {  // grain: solve serially below the cutoff
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
  }
  long long a, b;
  parlay::par_do([&]() { a = fib(n - 1); },
                 [&]() { b = fib(n - 2); });
  return a + b;
}

int main(int argc, char* argv[]) {
  long long n = (argc > 1) ? std::atoll(argv[1]) : 42;
  if (argc > 2) {
    burdened_vertex::burden = 1000ull * std::strtoull(argv[2], nullptr, 10);
  }

  long long result = 0;

  auto start_ts = std::chrono::high_resolution_clock::now();
  auto v = parlay::augment(burdened_vertex{}, [&]() {
    result = fib(n);
  });
  auto stop_ts = std::chrono::high_resolution_clock::now();
  double elapsed_ms =
      std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count() / 1e6;

  printf("fib(%lld) = %lld  (threads: %zu, burden: %llu us)\n\n",
         n, result, parlay::num_workers(), burdened_vertex::burden / 1000);
  printf("end-to-end time:        %10.3f ms\n", elapsed_ms);
  printf("work:                   %10.3f ms\n", v.work / 1e6);
  printf("burdened span:          %10.3f ms\n", v.span / 1e6);
  printf("estimated parallelism:  %10.1f\n",
         (v.span > 0) ? (double)v.work / (double)v.span : 0.0);
  return 0;
}
