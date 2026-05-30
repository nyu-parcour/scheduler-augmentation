#include <iostream>
#include <string>
#include <iomanip>

#include <parlay/primitives.h>
#include <parlay/sequence.h>

#include "low_diameter_decomposition.h"
#include "graph_utils.h"

// **************************************************************
// Driver
// **************************************************************
int main(int argc, char* argv[]) {
  using vertex = int;
  using graph = parlay::sequence<parlay::sequence<vertex>>;
  using utils = graph_utils<vertex>;

  auto usage = "Usage: low_diameter_decomposition <n> || low_diameter_decomposition <filename>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n = 0;
    graph G, GT;
    try { n = std::stol(argv[1]); }
    catch (...) {}
    if (n == 0) {
      G = utils::read_symmetric_graph_from_file(argv[1]);
      GT = G;
      n = G.size();
    } else {
      G = utils::rmat_graph(n, 20*n);
      GT = utils::transpose(G);
    }
    // utils::print_graph_stats(G);
    parlay::sequence<vertex> result;
    // parlay::internal::timer t("Time");
    // for (int i=0; i < 5; i++) {
    //   result = LDD<vertex>(.5, G, GT);
    //   t.next("low_diameter_decomposition");
    // }

    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      result = LDD<vertex>(.5, G, GT);
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto ret = parlay::augment([&]() {
        result = LDD<vertex>(.5, G, GT);
      });
      avg_elapsed_time += ret.second;
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // result = LDD<vertex>(.5, G, GT);
    // auto cluser_ids = parlay::remove_duplicates(result);
    // std::cout << "num clusters: " << cluser_ids.size() << std::endl;
  }
}
