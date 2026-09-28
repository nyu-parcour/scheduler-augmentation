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

// The schedule-independent measures of an augmented run (in cells, plus the
// raw byte figures), and the footprint this particular run reached.
struct measures {
  long long threads, delta, r1, r1_lr, rinf, footprint, bound;
  bool within_bound;
  long long gross_bytes, s1star_bytes, sinf_bytes, s1_bytes, footprint_bytes;
};

// A vertex that saw nothing means the computation ran outside the augmented
// region, or under a scheduler installing some other vertex type, so zero is
// treated as a harness bug. A program that can legitimately allocate nothing
// (par-clique on a graph with no oriented edges) passes expect_alloc = false.
inline measures collect(const parlay::space_vertex& v, bool expect_alloc = true) {
  if constexpr (parlay::augmentation_enabled) {
    if (expect_alloc && v.s1star == 0) {
      std::fprintf(stderr, "error: vertex recorded no allocation; "
                           "is the computation inside parlay::augment?\n");
      std::exit(1);
    }
  }

  measures m;
  m.threads = static_cast<long long>(parlay::num_workers());
  m.delta = static_cast<long long>(v.gross / cell_bytes);
  m.r1 = static_cast<long long>(v.s1star / cell_bytes);
  m.r1_lr = static_cast<long long>(v.s1 / cell_bytes);
  m.rinf = static_cast<long long>(v.sinf / cell_bytes);

  // The peak this particular run actually reached, and the bound R1 places on
  // it. Unlike the measures above this one is schedule-dependent.
  m.footprint = static_cast<long long>(parlay::space_high_water_bytes() / cell_bytes);
  m.bound = m.threads * m.r1;
  m.within_bound = m.footprint <= m.bound;

  m.gross_bytes = static_cast<long long>(v.gross);
  m.s1star_bytes = static_cast<long long>(v.s1star);
  m.sinf_bytes = static_cast<long long>(v.sinf);
  m.s1_bytes = static_cast<long long>(v.s1);
  m.footprint_bytes = static_cast<long long>(parlay::space_high_water_bytes());
  return m;
}

// The measure fields of the JSON line, "delta" through "ms", without braces,
// so that a port can print its own identifying fields ahead of them.
inline void print_measures_json(const measures& m, double ms) {
  std::printf("\"delta\":%lld,\"r1\":%lld,\"rinf\":%lld,\"r1_lr\":%lld,"
              "\"footprint\":%lld,\"bound\":%lld,\"within_bound\":%s,"
              "\"gross_bytes\":%lld,\"s1star_bytes\":%lld,\"sinf_bytes\":%lld,"
              "\"s1_bytes\":%lld,\"footprint_bytes\":%lld,\"ms\":%.3f",
              m.delta, m.r1, m.rinf, m.r1_lr,
              m.footprint, m.bound, m.within_bound ? "true" : "false",
              m.gross_bytes, m.s1star_bytes, m.sinf_bytes, m.s1_bytes,
              m.footprint_bytes, ms);
}

inline void print_measures_text(const measures& m, double ms) {
  std::printf("delta  %lld\n", m.delta);
  std::printf("R1     %lld\n", m.r1);
  std::printf("R1(LR) %lld\n", m.r1_lr);
  std::printf("Rinf   %lld\n", m.rinf);
  std::printf("footprint  %lld   (%s P*R1 = %lld)\n", m.footprint,
              m.within_bound ? "<=" : "EXCEEDS", m.bound);
  std::printf("time   %.0fms\n", ms);
}

// `value` is printed as a string so that it lines up with splang's JSON, which
// reports "()" for allocfree and a numeral for nqueens.
inline void report(const char* example, const options& o, const char* value,
                   const parlay::space_vertex& v, double ms) {
  const measures m = collect(v);
  if (o.json) {
    std::printf("{\"example\":\"%s\",\"size\":%lld,\"threads\":%lld,\"value\":\"%s\",",
                example, static_cast<long long>(o.size), m.threads, value);
    print_measures_json(m, ms);
    std::printf("}\n");
  }
  else {
    std::printf("size %lld, threads %lld\n", static_cast<long long>(o.size), m.threads);
    std::printf("value  %s\n", value);
    print_measures_text(m, ms);
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
