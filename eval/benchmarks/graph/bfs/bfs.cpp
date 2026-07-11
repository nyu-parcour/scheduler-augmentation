#include <iostream>
#include <string>
#include <iomanip>

#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <parlay/internal/get_time.h>

#include "bfs.h"
#include "graph_utils.h"
#include <chrono>
#include <parlay/work_span_vertex.h>

// **************************************************************
// Driver
// **************************************************************
using vertex = int;
using nested_seq = parlay::sequence<parlay::sequence<vertex>>;
using graph = nested_seq;
using utils = graph_utils<vertex>;

int main(int argc, char* argv[]) {
  auto usage = "Usage: BFS <n> || BFS <filename>";
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
      G = utils::rmat_graph(n, 20*n);
    }
    // utils::print_graph_stats(G);
    nested_seq result;
    // parlay::internal::timer t("Time");
    // for (int i=0; i < 3; i++) {
    //   result = BFS(1, G);
    //   t.next("BFS");
    // }

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto start_ts = std::chrono::high_resolution_clock::now();
      auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
        result = BFS(1, G);
      });
      auto stop_ts = std::chrono::high_resolution_clock::now();
      avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
      if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // result = BFS(1, G);
    // long visited = parlay::reduce(parlay::map(result, parlay::size_of()));
    // std::cout << "num vertices visited: " << visited << std::endl;
  }
}
