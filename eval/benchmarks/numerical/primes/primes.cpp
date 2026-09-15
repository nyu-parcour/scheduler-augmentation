#include <iostream>
#include <string>
#include <iomanip>

#include <parlay/sequence.h>

#include "primes.h"
#include <chrono>
#include <parlay/work_span_vertex.h>

// **************************************************************
// Driver code
// **************************************************************
int main(int argc, char* argv[]) {
  auto usage = "Usage: primes <n>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n;
    try { n = std::stol(argv[1]); }
    catch (...) { std::cout << usage << std::endl; return 1; }

    // std::cout << "generating all primes up to " << n << std::endl;
    // parlay::internal::timer t("Time");
    parlay::sequence<long> result;
    // for (int i=0; i < 5; i++) {
    //   result = primes(n);
    //   t.next("primes");
    // }

    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      result = primes(n);
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto start_ts = std::chrono::high_resolution_clock::now();
      auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
        result = primes(n);
      });
      auto stop_ts = std::chrono::high_resolution_clock::now();
      avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
      if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // result = primes(n);
    // std::cout << "number of primes: " << result.size() << std::endl;
  }
}
