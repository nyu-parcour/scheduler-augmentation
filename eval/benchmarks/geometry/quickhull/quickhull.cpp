#include <iostream>
#include <string>
#include <random>
#include <iomanip>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/internal/get_time.h>

#include "quickhull.h"

// **************************************************************
// Driver code
// **************************************************************
int main(int argc, char* argv[]) {
  auto usage = "Usage: quickhull <n>";
  if (argc != 2) std::cout << usage << std::endl;
  else {
    long n;
    try { n = std::stol(argv[1]); }
    catch (...) { std::cout << usage << std::endl; return 1; }

    // Points on a semicircle (upper half) with radius R, evenly spaced in angle.
    const double pi = 3.14159265358979323846;
    auto points = parlay::tabulate(n, [&] (long i) -> point {
      double theta = (pi * i) / static_cast<double>(n - 1);
      return point{std::cos(theta), std::sin(theta)};
    });

    auto ret = parlay::augment([&]() {
      auto results = upper_hull(points);
      std::cout << "upper_hull size: " << results.size() << std::endl;
    });
  }
}
