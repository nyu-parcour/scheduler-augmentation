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
  // kclique only; the other benchmarks ignore them.
  std::int64_t k = 0;
  bool fused_base = false;
  bool orient_inside = false;
  const char* graph = nullptr;  // positional
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
    else if (std::strcmp(argv[i], "--k") == 0 && i + 1 < argc) {
      o.k = std::atoll(argv[++i]);
    }
    else if (std::strcmp(argv[i], "--fused-base") == 0) {
      o.fused_base = true;
    }
    else if (std::strcmp(argv[i], "--orient-inside") == 0) {
      o.orient_inside = true;
    }
    else if (argv[i][0] != '-' && o.graph == nullptr) {
      o.graph = argv[i];
    }
    else {
      std::fprintf(stderr, "usage: %s [--size N] [--json] "
                           "[--k K] [--fused-base] [--orient-inside] [graph]\n", argv[0]);
      std::exit(2);
    }
  }
  return o;
}

// `value` is printed as a string so that it lines up with splang's JSON, which
// reports "()" for allocfree and a numeral for nqueens.
//
// expect_alloc is false only for a configuration that legitimately allocates
// nothing (kclique at k = 3 with --fused-base), where zero is the measurement.
inline void report(const char* example, const options& o, const char* value,
                   const parlay::space_vertex& v, double ms, bool expect_alloc = true) {
  // A vertex that saw nothing means the computation ran outside the augmented
  // region, or under a scheduler installing some other vertex type. Every
  // other program here allocates, so zero is a harness bug rather than a
  // measurement.
  if constexpr (parlay::augmentation_enabled) {
    if (expect_alloc && v.s1star == 0) {
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

  // The bound S + P*R1star_partial. S is the peak along the top-level spine
  // with each parallel block's own peak counted as 0, and R1star_partial is
  // the largest R1star of any block on it. It only applies when no block
  // frees memory that was live before it started (partial_min_prefix == 0).
  const long long s = static_cast<long long>(v.spine_seq / cell_bytes);
  const long long r1star_partial = static_cast<long long>(v.spine_par / cell_bytes);
  const long long partial_min_prefix = static_cast<long long>(v.spine_par_min / cell_bytes);
  const bool partial_applies = v.spine_par_min >= 0;
  const long long partial_bound = s + threads * r1star_partial;
  // Without augmentation S and R1star_partial are never recorded, so the bound is 0.
  if constexpr (parlay::augmentation_enabled) assert(footprint <= partial_bound);
  std::printf("%lld,%lld,%lld,%lld,%lld,%lld\n", static_cast<long long>(o.size), threads, footprint, s, partial_bound, rinf);

  // // The JSON also carries delta, r1 and the P*R1star bound, which compare.py
  // // checks against splang.
  // if (o.json) {
  //   std::printf("{\"example\":\"%s\",\"size\":%lld,\"threads\":%lld,\"value\":\"%s\","
  //               "\"footprint\":%lld,\"s\":%lld,\"r1star_partial\":%lld,"
  //               "\"partial_bound\":%lld,\"partial_applies\":%s,"
  //               "\"within_partial_bound\":%s,\"partial_min_prefix\":%lld,\"rinf\":%lld,"
  //               "\"delta\":%lld,\"r1\":%lld,\"r1_lr\":%lld,"
  //               "\"bound\":%lld,\"within_bound\":%s,"
  //               "\"gross_bytes\":%lld,\"s1star_bytes\":%lld,\"sinf_bytes\":%lld,"
  //               "\"s1_bytes\":%lld,\"footprint_bytes\":%lld,\"ms\":%.3f}\n",
  //               example, static_cast<long long>(o.size), threads, value,
  //               footprint, s, r1star_partial,
  //               partial_bound, partial_applies ? "true" : "false",
  //               within_partial_bound ? "true" : "false", partial_min_prefix, rinf,
  //               delta, r1, r1_lr,
  //               bound, within_bound ? "true" : "false",
  //               static_cast<long long>(v.gross), static_cast<long long>(v.s1star),
  //               static_cast<long long>(v.sinf), static_cast<long long>(v.s1),
  //               static_cast<long long>(parlay::space_high_water_bytes()), ms);
  // }
  // else {
  //   std::printf("size %lld, threads %lld\n", static_cast<long long>(o.size), threads);
  //   std::printf("%-22s %s\n", "value", value);
  //   std::printf("%-22s %lld   (observed HWM)\n", "footprint", footprint);
  //   std::printf("%-22s %lld\n", "S", s);
  //   std::printf("%-22s %lld\n", "R1star_partial", r1star_partial);
  //   if (partial_applies) {
  //     std::printf("%-22s %lld   (footprint %s)\n", "S + P*R1star_partial", partial_bound,
  //                 within_partial_bound ? "within" : "EXCEEDS");
  //   }
  //   else {
  //     std::printf("%-22s n/a   (a parallel block dips %lld cells below its start)\n",
  //                 "S + P*R1star_partial", -partial_min_prefix);
  //   }
  //   std::printf("%-22s %lld\n", "Rinf", rinf);
  //   std::printf("%-22s %.0fms\n", "time", ms);
  // }
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
