#include <iostream>
#include <string>
#include <random>
#include <limits>
#include <iomanip>

#include <parlay/primitives.h>
#include <parlay/random.h>

#include "karatsuba.h"

// **************************************************************
// Driver code
// **************************************************************
int main(int argc, char* argv[]) {
  auto usage = "Usage: karatsuba <size>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n;
    try { n = std::stol(argv[1]); }
    catch (...) { std::cout << usage << std::endl; return 1; }

    long m = n/digit_len;

    auto randnum = [] (long m, long seed) {
      parlay::random_generator gen(seed);
      auto maxv = std::numeric_limits<digit>::max();
      std::uniform_int_distribution<digit> dis(0, maxv);
      return parlay::tabulate(m, [&] (long i) {
        auto r = gen[i];
        if (i == m-1) return dis(r)/2; // to ensure it is not negative
        else return dis(r);});
    };

    bigint a = randnum(m, 0);
    bigint b = randnum(m, 1);
    bigint result;

    // parlay::internal::timer t("Time");
    // for (int i=0; i < 5; i++) {
    //   result = karatsuba(a, b);
    //   t.next("karatsuba");
    // }

    parlay::internal::timer t;
    double delay = 3.0;
    // run for delay seconds to "warm things up"
    while (t.total_time() < delay) {
      result = karatsuba(a, b);
    } 

    int num_iters = 20;
    double avg_elapsed_time = 0.0;
    for (int i = 0; i < num_iters; i++) {
      auto ret = parlay::augment([&]() {
        result = karatsuba(a, b);
      });
      avg_elapsed_time += ret.second;
    }
    avg_elapsed_time /= num_iters;
    std::cout << std::fixed << std::setprecision(2) << avg_elapsed_time << "\n";
    
    // result = karatsuba(a, b);
  }
}
