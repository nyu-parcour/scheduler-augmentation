#include <iostream>
#include <iomanip>

#include <parlay/io.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <parlay/internal/get_time.h>

#include "kmp.h"

// **************************************************************
// Driver code
// **************************************************************

int main(int argc, char* argv[]) {
  auto usage = "Usage: knuth_morris_pratt <search_string> <filename>";
  if (argc != 3) std::cout << usage << std::endl;
  else {
    parlay::chars str = parlay::chars_from_file(argv[2]);
    parlay::chars search_str = parlay::to_chars(argv[1]);
    parlay::sequence<long> locations;
    // parlay::internal::timer t("Time");
    // for (int i=0; i < 5; i++) {
    //   locations = knuth_morris_pratt(str, search_str);
    //   t.next("knuth_morris_pratt");
    // }

    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      locations = knuth_morris_pratt(str, search_str);
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto ret = parlay::augment([&]() {
        locations = knuth_morris_pratt(str, search_str);
      });
      avg_elapsed_time += ret.second;
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // locations = knuth_morris_pratt(str, search_str);
    // std::cout << "total matches = " << locations.size() << std::endl;
    // if (locations.size() > 10) {
    //   auto r = parlay::to_sequence(locations.cut(0,10));
    //   std::cout << "at locations: " << to_chars(r) << " ..." <<  std::endl;
    // } else if (locations.size() > 0) {
    //   std::cout << "at locations: " << to_chars(locations) << std::endl;
    // }
  }
}
