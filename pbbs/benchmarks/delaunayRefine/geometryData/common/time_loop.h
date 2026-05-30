#include "../parlay/internal/get_time.h"
#include "../parlay/parallel.h"
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
    auto ret = parlay::augment([&]() {
      runf();
    });
    avg_elapsed_time += ret.second;
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
