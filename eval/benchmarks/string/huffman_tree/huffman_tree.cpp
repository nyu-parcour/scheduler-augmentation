#include <iostream>
#include <string>
#include <random>
#include <tuple>
#include <iomanip>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>
#include <parlay/internal/get_time.h>

#include "huffman_tree.h"
#include <chrono>
#include <parlay/work_span_vertex.h>

// **************************************************************
// Driver
// **************************************************************
int main(int argc, char* argv[]) {
  auto usage = "Usage: huffman_tree <num_points>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n;
    try { n = std::stol(argv[1]); }
    catch (...) { std::cout << usage << std::endl; return 1; }
    parlay::random_generator gen;
    std::uniform_real_distribution<float> dis(1.0, static_cast<float>(n));

    // generate unnormalized probabilities
    auto probs = parlay::tabulate(n, [&] (long i) -> float {
      auto r = gen[i];
      return 1.0/dis(r);
    });

    // normalize them
    float total = parlay::reduce(probs);
    probs = parlay::map(probs, [&] (float p) {return p/total;});

    std::tuple<parlay::sequence<node*>,node*> result;

    // parlay::internal::timer t("Time");
    // for (int i=0; i < 5; i++) {
    //   t.start();
    //   result = huffman_tree(probs);
    //   t.next("huffman_tree");
    //   delete_tree(std::get<1>(result));
    // }

    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      result = huffman_tree(probs);
      delete_tree(std::get<1>(result));
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto start_ts = std::chrono::high_resolution_clock::now();
      auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
        result = huffman_tree(probs);
      });
      auto stop_ts = std::chrono::high_resolution_clock::now();
      delete_tree(std::get<1>(result));
      avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
      if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // result = huffman_tree(probs);
    // // t.next("huffman_tree");
    // delete_tree(std::get<1>(result));
    
  }
}
