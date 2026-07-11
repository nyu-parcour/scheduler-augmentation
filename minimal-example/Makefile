INCLUDE := ../include

CXX      ?= g++
CXXFLAGS := -O2 -std=c++17 -I$(INCLUDE) -pthread

fib: main.cpp burdened_vertex.h
	$(CXX) $(CXXFLAGS) main.cpp -o fib

run: fib
	./fib

clean:
	rm -f fib

.PHONY: run clean
