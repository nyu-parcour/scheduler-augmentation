#include <iostream>
#include <string>
#include <iomanip>

#include <parlay/primitives.h>
#include <parlay/sequence.h>

#include "triangle_count.h"
#include "graph_utils.h"
#include <chrono>
#include <parlay/work_span_vertex.h>

// **************************************************************
// Driver
// **************************************************************
int main(int argc, char* argv[]) {
  using vertex = int;
  using graph = parlay::sequence<parlay::sequence<vertex>>;
  using utils = graph_utils<vertex>;

  auto usage = "Usage: triangle_count <n> || triangle_count <filename>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n = 0;
    graph G;
    try { n = std::stol(argv[1]); }
    catch (...) {}
    if (n == 0) {
      G = utils::read_symmetric_graph_from_file(argv[1]);
      n = G.size();
    } else {
      G = utils::rmat_symmetric_graph(n, 20*n);
    }
    // utils::print_graph_stats(G);
    long count;
    // parlay::internal::timer t("Time");
    // for (int i=0; i < 1; i++) {
    //   count = triangle_count(G);
    //   t.next("triangle count");
    // }


    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      count = triangle_count(G);
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto start_ts = std::chrono::high_resolution_clock::now();
      auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
        count = triangle_count(G);
      });
      auto stop_ts = std::chrono::high_resolution_clock::now();
      avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
      if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // count = triangle_count(G);
    // std::cout << "number of triangles: " << count << std::endl;
  }
}
