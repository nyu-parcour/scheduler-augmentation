#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

echo "==> Initializing submodules..."
git submodule update --init --recursive

echo "==> Building benchmarks..."
make -C pbbs rangeQuery2d/parallelPlaneSweep rangeQuery2d/parallelPlaneSweepGrainFix

echo "==> Generating input data..."
make -C pbbs geometryData
pbbs/testData/geometryData/randPoints 1000000 pbbs/testData/geometryData/uniform-square-1M

echo "==> Running original (parallelPlaneSweep)..."
PARLAY_NUM_THREADS=1 pbbs/benchmarks/rangeQuery2d/parallelPlaneSweep/range \
    pbbs/testData/geometryData/uniform-square-1M > out
./extract_last_range.sh out > rq2d_all_diamond

echo "==> Running with grain heuristic (parallelPlaneSweepGrainFix)..."
PARLAY_NUM_THREADS=1 pbbs/benchmarks/rangeQuery2d/parallelPlaneSweepGrainFix/range \
    pbbs/testData/geometryData/uniform-square-1M > out
./extract_last_range.sh out > rq2d_ours_all_diamond

echo "==> Plotting..."
./MAKE-PLOTS-FOR-PAPER.sh

echo "==> Done."
