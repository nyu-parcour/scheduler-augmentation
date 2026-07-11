#include <iostream>
#include <string>
#include <random>
#include <iomanip>

#include <parlay/io.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>

#include "mergesort.h"
#include <chrono>
#include <parlay/work_span_vertex.h>

// **************************************************************
// Driver
// **************************************************************
int main(int argc, char* argv[]) {
  auto usage = "Usage: mergesort <n>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n;
    try { n = std::stol(argv[1]); }
    catch (...) { std::cout << usage << std::endl; return 1; }
    parlay::random_generator gen;
    std::uniform_int_distribution<long> dis(0, n-1);

    // generate random long values
    auto data = parlay::tabulate(n, [&] (long i) {
      auto r = gen[i];
      return dis(r);});

    // parlay::internal::timer t("Time");
    parlay::sequence<long> result;
    // for (int i=0; i < 5; i++) {
    //   result = data;
    //   t.start();
    //   merge_sort(result);
    //   t.next("mergesort");
    // }

    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      result = data;
      merge_sort(result);
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      result = data;
      auto start_ts = std::chrono::high_resolution_clock::now();
      auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
        merge_sort(result);
      });
      auto stop_ts = std::chrono::high_resolution_clock::now();
      avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
      if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // result = data;
    // merge_sort(result);
    // auto first_ten = result.head(10);

    // std::cout << "first 10 elements: " << parlay::to_chars(first_ten) << std::endl;
  }
}
