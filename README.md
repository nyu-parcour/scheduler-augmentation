# Scheduler Augmentation

This repository contains the source code for the following paper 
> *Scheduler Augmentation: A Lightweight, Customizable, Low-Cost Profiling Technique for Fork-Join Parallel Programs.* 
> Sam Westrick, Darshan Dinesh Kumar and Seong-Heon Jung.
> ACM Symposium on Parallelism in Algorithms and Architectures (SPAA) 2026.

# Repository Structure

* It starts from a fork of [ParlayLib](https://github.com/cmuparlay/parlaylib).
* The [`include/parlay/scheduler.h`](include/parlay/scheduler.h) file contains the changes implemented to extend ParlayLib's work-stealing scheduler with scheduler augmentation.
* The [`include/parlay/internal/vertex.h`](include/parlay/internal/vertex.h) file contains the Vertex definition
* The `eval` directory contains the ParlayLib benchmarks and related scripts for experimentation.
* The `pbbs` directory contains the [PBBS](https://github.com/cmuparlay/pbbsbench) benchmarks and related scripts for experimentation.
* 3 branches with the following objectives:
    * `master` branch: Contains the `EvaluationVertex`, measuring work, span, and the number of forks. This branch can be used to reproduce the results of the evaluation section (section 6) of the paper.
    * `grain-analysis` branch: Contains the `GrainAnalysisVertex` used to perform granularity analysis. This branch can be used to reproduce the results of the granularity analysis section and the parallel range query case study (sections 3 and 3.1) of the paper.
    * `space-profiling` branch: Contains the `SpaceVertex` used to perform space profiling. This branch can be used to reproduce the results of the space profiling section and the quickhull case study (sections 4 and 4.1) of the paper.
* Each branch's README contains the necessary details to reproduce the results 

# How to Run

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