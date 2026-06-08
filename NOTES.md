
make -C pbbs rangeQuery2d/parallelPlaneSweep{,GrainFix}
make -C pbbs geometryData
pbbs/testData/geometryData/randPoints 1000000 pbbs/testData/geometryData/uniform-square-1M

PARLAY_NUM_THREADS=1 pbbs/benchmarks/rangeQuery2d/parallelPlaneSweep/range pbbs/testData/geometryData/uniform-square-1M > out && ./extract_last_range.sh out > rq2d_all_diamond

PARLAY_NUM_THREADS=1 pbbs/benchmarks/rangeQuery2d/parallelPlaneSweepGrainFix/range pbbs/testData/geometryData/uniform-square-1M > out && ./extract_last_range.sh out > rq2d_ours_all_diamond

MAKE-PLOTS-FOR-PAPER.sh