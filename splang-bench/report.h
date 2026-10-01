// Shared CLI and reporting for the splang benchmark ports.
//
// The output is designed to be diffed directly against `splang --json`: the
// delta/r1/rinf fields are in splang cells, where one cell is one machine word.

#ifndef SPLANG_BENCH_REPORT_H_
#define SPLANG_BENCH_REPORT_H_

#include <cassert>
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
  long long s, r1star_partial, partial_min_prefix, partial_bound;
  bool footprint_tracked, within_bound, partial_applies, within_partial_bound;
  long long gross_bytes, s1star_bytes, sinf_bytes, s1_bytes, footprint_bytes;
  long long s_bytes, r1star_partial_bytes;
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

  m.gross_bytes = static_cast<long long>(v.gross);
  m.s1star_bytes = static_cast<long long>(v.s1star);
  m.sinf_bytes = static_cast<long long>(v.sinf);
  m.s1_bytes = static_cast<long long>(v.s1);
  m.footprint_bytes = static_cast<long long>(parlay::space_high_water_bytes());

  // The bound S + P*R1star_partial. S is the peak along the top-level spine
  // with each parallel block's own peak counted as 0, and R1star_partial is
  // the largest R1star of any block on it. It only applies when no block
  // frees memory that was live before it started (partial_min_prefix == 0).
  m.s = static_cast<long long>(v.spine_seq / cell_bytes);
  m.r1star_partial = static_cast<long long>(v.spine_par / cell_bytes);
  m.partial_min_prefix = static_cast<long long>(v.spine_par_min / cell_bytes);
  m.partial_applies = v.spine_par_min >= 0;
  m.partial_bound = m.s + m.threads * m.r1star_partial;
  m.s_bytes = static_cast<long long>(v.spine_seq);
  m.r1star_partial_bytes = static_cast<long long>(v.spine_par);

  // Both bounds are checked in bytes. The cell figures are floored, and a port
  // whose buffers are not whole cells (par-clique's u32 ids) can have
  // P * floor(R1) fall short of floor(P * R1), which would flag a run that is
  // within the bound.
  m.within_bound = m.footprint_bytes <= m.threads * m.s1star_bytes;
  m.within_partial_bound =
      m.footprint_bytes <= m.s_bytes + m.threads * m.r1star_partial_bytes;

  // With footprint tracking off (parlay::space_track_footprint) there is no
  // footprint to report, and none to check against the bound.
  m.footprint_tracked = parlay::space_footprint_tracked();
  return m;
}

// The measure fields of the JSON line, "delta" through "ms", without braces,
// so that a port can print its own identifying fields ahead of them. The
// footprint fields are null when footprint tracking was off.
inline void print_measures_json(const measures& m, double ms) {
  std::printf("\"delta\":%lld,\"r1\":%lld,\"rinf\":%lld,\"r1_lr\":%lld,"
              "\"s\":%lld,\"r1star_partial\":%lld,\"partial_min_prefix\":%lld,"
              "\"partial_applies\":%s,\"bound\":%lld,\"partial_bound\":%lld,",
              m.delta, m.r1, m.rinf, m.r1_lr,
              m.s, m.r1star_partial, m.partial_min_prefix,
              m.partial_applies ? "true" : "false", m.bound, m.partial_bound);
  if (m.footprint_tracked) {
    std::printf("\"footprint\":%lld,\"within_bound\":%s,\"within_partial_bound\":%s,",
                m.footprint, m.within_bound ? "true" : "false",
                m.within_partial_bound ? "true" : "false");
  }
  else {
    std::printf("\"footprint\":null,\"within_bound\":null,\"within_partial_bound\":null,");
  }
  std::printf("\"gross_bytes\":%lld,\"s1star_bytes\":%lld,\"sinf_bytes\":%lld,"
              "\"s1_bytes\":%lld,\"s_bytes\":%lld,\"r1star_partial_bytes\":%lld,",
              m.gross_bytes, m.s1star_bytes, m.sinf_bytes, m.s1_bytes,
              m.s_bytes, m.r1star_partial_bytes);
  if (m.footprint_tracked) std::printf("\"footprint_bytes\":%lld,", m.footprint_bytes);
  else std::printf("\"footprint_bytes\":null,");
  std::printf("\"ms\":%.3f", ms);
}

inline void print_measures_text(const measures& m, double ms) {
  std::printf("%-22s %lld\n", "delta", m.delta);
  std::printf("%-22s %lld\n", "R1star", m.r1);
  std::printf("%-22s %lld\n", "R1(LR)", m.r1_lr);
  std::printf("%-22s %lld\n", "Rinf", m.rinf);
  std::printf("%-22s %lld\n", "S", m.s);
  std::printf("%-22s %lld\n", "R1star_partial", m.r1star_partial);
  if (m.footprint_tracked) {
    std::printf("%-22s %lld   (observed HWM)\n", "footprint", m.footprint);
    std::printf("%-22s %lld   (footprint %s)\n", "P*R1star", m.bound,
                m.within_bound ? "within" : "EXCEEDS");
  }
  else {
    std::printf("%-22s not tracked\n", "footprint");
    std::printf("%-22s %lld\n", "P*R1star", m.bound);
  }
  if (!m.partial_applies) {
    std::printf("%-22s n/a   (a parallel block dips %lld cells below its start)\n",
                "S + P*R1star_partial", -m.partial_min_prefix);
  }
  else if (m.footprint_tracked) {
    std::printf("%-22s %lld   (footprint %s)\n", "S + P*R1star_partial", m.partial_bound,
                m.within_partial_bound ? "within" : "EXCEEDS");
  }
  else {
    std::printf("%-22s %lld\n", "S + P*R1star_partial", m.partial_bound);
  }
  std::printf("%-22s %.0fms\n", "time", ms);
}

// `value` is printed as a string so that it lines up with splang's JSON, which
// reports "()" for allocfree and a numeral for nqueens.
//
// Without --json the output is one CSV line,
// size,threads,footprint,S,S+P*R1star_partial,Rinf, which the *_run.sh scripts
// collect for the *_plot.gp plots.
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
    assert(m.footprint <= m.partial_bound);
    std::printf("%lld,%lld,%lld,%lld,%lld,%lld\n", static_cast<long long>(o.size),
                m.threads, m.footprint, m.s, m.partial_bound, m.rinf);
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
