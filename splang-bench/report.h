// Shared CLI and reporting for the splang benchmark ports.
//
// The output is designed to be diffed directly against `splang --json`: the
// delta/r1/rinf fields are in splang cells, where one cell is one machine word.

#ifndef SPLANG_BENCH_REPORT_H_
#define SPLANG_BENCH_REPORT_H_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <chrono>

#include <parlay/parallel.h>
#include <parlay/vertices/space_alloc.h>
#include <parlay/vertices/space_vertex.h>

namespace splang_bench {

// One splang cell is one Val, which we represent as one int64_t.
constexpr std::int64_t cell_bytes = static_cast<std::int64_t>(sizeof(std::int64_t));

struct options {
  std::int64_t size;
  bool json = false;
};

inline options parse_args(int argc, char** argv, std::int64_t default_size) {
  options o{default_size, false};
  for (int i = 1; i < argc; i++) {
    if ((std::strcmp(argv[i], "--size") == 0 || std::strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
      o.size = std::atoll(argv[++i]);
    }
    else if (std::strcmp(argv[i], "--json") == 0) {
      o.json = true;
    }
    else {
      std::fprintf(stderr, "usage: %s [--size N] [--json]\n", argv[0]);
      std::exit(2);
    }
  }
  return o;
}

// `value` is printed as a string so that it lines up with splang's JSON, which
// reports "()" for allocfree and a numeral for nqueens.
inline void report(const char* example, const options& o, const char* value,
                   const parlay::space_vertex& v, double ms) {
  // A vertex that saw nothing means the computation ran outside the augmented
  // region, or under a scheduler installing some other vertex type. Every
  // program here allocates, so zero is always a harness bug rather than a
  // measurement.
  if constexpr (parlay::augmentation_enabled) {
    if (v.s1star == 0) {
      std::fprintf(stderr, "error: vertex recorded no allocation; "
                           "is the computation inside parlay::augment?\n");
      std::exit(1);
    }
  }

  const long long threads = static_cast<long long>(parlay::num_workers());
  const long long delta = static_cast<long long>(v.gross / cell_bytes);
  const long long r1 = static_cast<long long>(v.s1star / cell_bytes);
  const long long r1_lr = static_cast<long long>(v.s1 / cell_bytes);
  const long long rinf = static_cast<long long>(v.sinf / cell_bytes);

  // The peak this particular run actually reached, and the bound R1 places on
  // it. Unlike the measures above this one is schedule-dependent.
  const long long footprint =
      static_cast<long long>(parlay::space_high_water_bytes() / cell_bytes);
  const long long bound = threads * r1;
  const bool within_bound = footprint <= bound;

  if (o.json) {
    std::printf("{\"example\":\"%s\",\"size\":%lld,\"threads\":%lld,\"value\":\"%s\","
                "\"delta\":%lld,\"r1\":%lld,\"rinf\":%lld,\"r1_lr\":%lld,"
                "\"footprint\":%lld,\"bound\":%lld,\"within_bound\":%s,"
                "\"gross_bytes\":%lld,\"s1star_bytes\":%lld,\"sinf_bytes\":%lld,"
                "\"s1_bytes\":%lld,\"footprint_bytes\":%lld,\"ms\":%.3f}\n",
                example, static_cast<long long>(o.size), threads, value,
                delta, r1, rinf, r1_lr,
                footprint, bound, within_bound ? "true" : "false",
                static_cast<long long>(v.gross), static_cast<long long>(v.s1star),
                static_cast<long long>(v.sinf), static_cast<long long>(v.s1),
                static_cast<long long>(parlay::space_high_water_bytes()), ms);
  }
  else {
    std::printf("size %lld, threads %lld\n", static_cast<long long>(o.size), threads);
    std::printf("value  %s\n", value);
    std::printf("delta  %lld\n", delta);
    std::printf("R1     %lld\n", r1);
    std::printf("R1(LR) %lld\n", r1_lr);
    std::printf("Rinf   %lld\n", rinf);
    std::printf("footprint  %lld   (%s P*R1 = %lld)\n", footprint,
                within_bound ? "<=" : "EXCEEDS", bound);
    std::printf("time   %.0fms\n", ms);
  }
}

// Keeps a store to memory that is about to be freed from being optimized away.
inline void do_not_optimize(void* p) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "r,m"(p) : "memory");
#else
  static volatile void* sink;
  sink = p;
#endif
}

class timer {
 public:
  double ms() const {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start_).count();
  }
 private:
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace splang_bench

#endif  // SPLANG_BENCH_REPORT_H_
