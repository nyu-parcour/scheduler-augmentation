# Modifications
This branch contains the space profiling vertex
along with changes to `alloc.h` for calling the `Vertex.alloc` and `Vertex.free`
methods.

To build the augmented quickhull program,
move to the `eval/benchmarks` directory and run `make bin/quickhull.aug`
to create the `bin/quickhull.aug` binary.
To reproduce the plots, run `eval/scripts/space_usage.py`
This will output `eval/scripts/space_usage_plot.pdf`: the plot seen in the paper.

