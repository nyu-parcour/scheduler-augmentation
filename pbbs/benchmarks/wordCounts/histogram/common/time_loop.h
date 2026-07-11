#include "../parlay/internal/get_time.h"
#include "../parlay/parallel.h"
#include "../parlay/work_span_vertex.h"
#include <chrono>
#include <fstream>

template<class F, class G, class H>
void time_loop(int rounds, double delay, F initf, G runf, H endf) {
  parlay::internal::timer t;
  delay = 3.0;
  // run for delay seconds to "warm things up"
  // will skip if delay is zero
  while (t.total_time() < delay) {
    initf(); runf(); endf();
  }

  double avg_elapsed_time = 0.0;
  for (int i=0; i < rounds; i++) {
    initf();
    t.start();
    auto start_ts = std::chrono::high_resolution_clock::now();
    auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
      runf();
    });
    auto stop_ts = std::chrono::high_resolution_clock::now();
    avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
    if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    t.next("");
    endf();
  }

  avg_elapsed_time /= rounds;

  std::string filename = "avg_timing.txt";
  std::ofstream outfile;
  outfile.open(filename, std::ios::app);

  if (!outfile.is_open()) {
      std::cerr << "Error: Could not open " << filename << std::endl;
      exit(1);
  }
  outfile << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";
}
