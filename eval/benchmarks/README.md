The Makefile provides targets for building binaries, executing benchmarks, and generating performance graphs.

* **Build Binaries:** Run `make suite` to build all standard test binaries. You can also build individually via `make bin/<testname>`.
* **Run Benchmarks:** Run `make run` to execute all tests and output raw data to the `result/` directory. You can run a specific test using `make result/<category>_<testname>` (e.g., `make result/graph_triangle_count`).
* **Generate Graphs & Tables (End-to-End):** Run `make gen_graphs`. This is the recommended pipeline: it automatically executes `run` to collect data and then uses the python scripts in `../scripts/` to generate overhead tables and speedup plots in the `summary_plots/` directory.

**Result File Structure:**
Running `make result/<testname>` produces a single result file combining both execution times and logs. The top lines contain thread counts (1, 10, 20... 80) and their corresponding timings. For augmented tests (`.aug`), the vertex and scheduling logs (parsed from `logs_vertex.txt`) are automatically appended to the bottom of the same file.