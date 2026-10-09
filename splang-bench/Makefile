INCLUDE := ../include

CXX      ?= g++
# space_vertex.h includes spdlog, which is vendored under include/spdlog/include
# and is not on any default include path (cf. eval/benchmarks/Makefile).
# space_vertex is 64 bytes, over dynamic_vertex's default 48-byte inline payload.
CXXFLAGS := -O2 -std=c++17 -I$(INCLUDE) -I$(INCLUDE)/spdlog/include -pthread \
            -DPARLAY_DYNAMIC_VERTEX_CAPACITY=64

BINS := allocfree nqueens strassen karatsuba ann mergefeyn
# One plot per result CSV; the CSVs themselves come from run.sh.
PLOTS := $(patsubst %-result.csv,%-result.pdf,$(wildcard *-result.csv))

all: $(BINS) plots

plots: $(PLOTS)

%-result.pdf: %-result.csv plot.gp
	gnuplot -c plot.gp $*

allocfree: allocfree.cpp report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) allocfree.cpp -o $@

nqueens: nqueens.cpp report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) nqueens.cpp -o $@

strassen: strassen.cpp strassen.h report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) strassen.cpp -o $@

karatsuba: karatsuba.cpp karatsuba.h bigint_add.h report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) karatsuba.cpp -o $@

ann: ann.cpp report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) ann.cpp -o $@

mergefeyn: mergefeyn.cpp mergefeyn.h report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) mergefeyn.cpp -o $@

check: $(BINS)
	python3 compare.py

clean:
	rm -f $(BINS) $(PLOTS)

.PHONY: all plots check clean
