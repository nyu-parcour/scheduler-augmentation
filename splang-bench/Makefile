INCLUDE := ../include

CXX      ?= g++
# space_vertex.h includes spdlog, which is vendored under include/spdlog/include
# and is not on any default include path (cf. eval/benchmarks/Makefile).
CXXFLAGS := -O2 -std=c++17 -I$(INCLUDE) -I$(INCLUDE)/spdlog/include -pthread

BINS := allocfree nqueens strassen

all: $(BINS)

allocfree: allocfree.cpp report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) allocfree.cpp -o $@

nqueens: nqueens.cpp report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) nqueens.cpp -o $@

strassen: strassen.cpp strassen.h report.h $(INCLUDE)/parlay/vertices/space_alloc.h
	$(CXX) $(CXXFLAGS) strassen.cpp -o $@

check: $(BINS)
	python3 compare.py

clean:
	rm -f $(BINS)

.PHONY: all check clean
