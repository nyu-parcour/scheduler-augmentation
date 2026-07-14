# Scheduler Augmentation

![Example computation graph visualization](./example.gif)

This repository contains the source code for the following paper 
> *Scheduler Augmentation: A Lightweight, Customizable, Low-Cost Profiling Technique for Fork-Join Parallel Programs.* 
> Sam Westrick, Darshan Dinesh Kumar and Seong-Heon Jung.
> ACM Symposium on Parallelism in Algorithms and Architectures (SPAA) 2026.
> https://dl.acm.org/doi/10.1145/3816782.3819212

This repository has 5 branches:
  * [`master`](https://github.com/nyu-parcour/scheduler-augmentation/tree/master): implementation and performance evaluation from the paper
  * [`grain-analysis`](https://github.com/nyu-parcour/scheduler-augmentation/tree/grain-analysis): implementation of the granularity analysis technique from the paper
  * [`space-profiling`](https://github.com/nyu-parcour/scheduler-augmentation/tree/space-profiling): implementation of the space profiling technique from the paper
  * [`graph-viz`](https://github.com/nyu-parcour/scheduler-augmentation/tree/graph-viz): computation graph visualization, for example as shown above.
  * [`dynamic-vertex`](https://github.com/nyu-parcour/scheduler-augmentation/tree/dynamic-vertex): a more ergonomic interface, parameterizing the scheduler by a vertex container that can be dynamically instantiated. See the [minimal example](https://github.com/nyu-parcour/scheduler-augmentation/tree/dynamic-vertex/minimal-example) on this branch.

# Citation

If you use Scheduler Augmentation, we would appreciate a citation:
```bibtex
@inproceedings{wkj26-sched-aug,
  author = {Westrick, Sam and Kumar, Darshan Dinesh and Jung, Seong-Heon},
  title = {Scheduler Augmentation: A Lightweight, Customizable, Low-Cost Profiling Technique for Fork-Join Parallel Programs},
  year = {2026},
  isbn = {9798400727610},
  publisher = {Association for Computing Machinery},
  address = {New York, NY, USA},
  url = {https://doi.org/10.1145/3816782.3819212},
  doi = {10.1145/3816782.3819212},
  booktitle = {Proceedings of the 38th ACM Symposium on Parallelism in Algorithms and Architectures},
  pages = {457–472},
  numpages = {16},
  keywords = {work-stealing, scheduling, fork-join, parallelism, lightweight libraries, profiling},
  location = {Royal Holloway, University of London, London, United Kingdom},
  series = {SPAA '26}
}
```

# `master` Branch Details

* It starts from a fork of [ParlayLib](https://github.com/cmuparlay/parlaylib).
* The [`include/parlay/scheduler.h`](include/parlay/scheduler.h) file contains the changes implemented to extend ParlayLib's work-stealing scheduler with scheduler augmentation.
* The [`include/parlay/internal/vertex.h`](include/parlay/internal/vertex.h) file contains the Vertex definition
* The `eval` directory contains the ParlayLib benchmarks and related scripts for experimentation.
* The `pbbs` directory contains the [PBBS](https://github.com/cmuparlay/pbbsbench) benchmarks and related scripts for experimentation.

## How to Run

1. **Clone the repository**
   
2. **Install the required Python modules**
   ```bash
   pip3 install -r requirements.txt
   ```

3. **Retrieve the submodules**
   ```bash
   git submodule update --init --recursive
   ```

4. **Run and generate the results for the ParlayLib benchmarks**
   ```bash
   cd scheduler-augmentation/eval/benchmarks
   make gen_graphs
   ```
   * `scheduler-augmentation/eval/benchmarks/result` directory will contain the generated raw result files.
   * `scheduler-augmentation/eval/benchmarks/summary_plots` directory will contain the generated plots and tables:
     * `overhead_plot.png`: The overhead plot.
     * `speedup_subplots.png`: Contains the generated speedup subplots.
     * `parlay_table_1_40_80.txt`: The table with results for $P = 1, 40, 80$.
     * `parlay_table_all.txt`: The table with results for all processor counts (i.e., $P = 1, 10, 20, 30, 40, 50, 60, 70, 80$).

5. **Run and generate the results for the PBBS benchmarks**
   ```bash
   cd scheduler-augmentation/pbbs/scripts
   nohup ./main_script.sh &
   ```
   * `scheduler-augmentation/pbbs/result` directory will contain the generated results, tables, and the plot:
     * `run_unaug.out` and `run_aug.out`: Logs from the unaugmented and augmented benchmark executions.
     * `unaug.json` and `aug.json`: Processor-wise raw timing results for each benchmark.
     * `pbbs_table_1_40_80.txt`: The table with PBBS results for $P = 1, 40, 80$.
     * `pbbs_table_all.txt`: The table with PBBS results for all processor counts (i.e., $P = 1, 10, 20, 30, 40, 50, 60, 70, 80$).
     * `combined_table_all.txt`: The concatenated results table featuring both ParlayLib and PBBS benchmarks.
     * `overhead_plot.pdf`: The complete evaluation overhead plot (formatted exactly like Figure 12 in the paper).

> [!NOTE]  
> In order to correctly generate `combined_table_all.txt` and the final `overhead_plot.pdf` (Figure 12), you **must** generate the ParlayLib benchmark results *before* running the PBBS benchmark scripts.