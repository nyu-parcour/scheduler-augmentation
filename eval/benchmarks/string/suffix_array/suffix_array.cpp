#include <ctype.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <iomanip>

#include <parlay/io.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <parlay/internal/get_time.h>

#include "suffix_array.h"
#include <chrono>
#include <parlay/work_span_vertex.h>

// **************************************************************
// Driver code
// **************************************************************
using charseq = parlay::sequence<char>;
using uint = unsigned int;

auto check(parlay::sequence<char>& str) {
  auto ustr = parlay::map(str, [] (char x) {return (unsigned char) x;});
  auto less = [&] (uint i, uint j) {
    return parlay::lexicographical_compare(ustr.cut(i,ustr.size()),ustr.cut(j,ustr.size()));};
  return parlay::sort(parlay::iota<uint>(ustr.size()), less);
}

int main(int argc, char* argv[]) {
  auto usage = "Usage: suffix_array <filename>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    charseq str = parlay::chars_from_file(argv[1]);
    using index = unsigned int;
    long n = str.size();
    parlay::sequence<index> result;

    // parlay::internal::timer t("Time");
    // for (int i=0; i < 5; i++) {
    //   result = suffix_array(str);
    //   t.next("suffix_array");
    // }

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto start_ts = std::chrono::high_resolution_clock::now();
      auto v = parlay::augment(parlay::work_span_vertex{}, [&]() {
        result = suffix_array(str);
      });
      auto stop_ts = std::chrono::high_resolution_clock::now();
      avg_elapsed_time += std::chrono::duration_cast<std::chrono::nanoseconds>(stop_ts - start_ts).count();
      if constexpr (parlay::augmentation_enabled) v.log(parlay::num_workers());
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";

    // result = suffix_array(str);
    
    // // take first n entries
    // auto cnt = std::min<long>(10, n);
    // auto head = result.head(cnt);
    // std::cout << "first 10 entries: " << to_chars(head) << std::endl;

    // // check correctness if not too long
    // if (n <= 1000000 && check(str) != result)
    //   std::cout << "check failed" << std::endl;
  }
}
