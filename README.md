# Scheduler Augmentation

This repository contains the source code for the paper *Scheduler Augmentation: A Lightweight, Customizable, Low-Cost Profiling Technique for Fork-Join Parallel Programs" by Sam Westrick, Darshan Dinesh Kumar and Seong-Heon Jung*, accepted into the ACM Symposium on Parallelism in Algorithms and Architectures (SPAA) 2026.

# Repository Structure

* It starts from a fork of [ParlayLib](https://github.com/cmuparlay/parlaylib)
* The include/parlay/scheduler.h contains the changes implemented to extend the work-stealing scheduler with scheduler augmentation
* The include/parlay/internal/vertex.h file contains the EvaluationVertex, measuring work, span, and the number of forks
* The eval directory contains the ParlayLib benchmarks and related scripts for experimentation
* The pbbs directory contains the [PBBS](https://github.com/cmuparlay/pbbsbench) benchmarks and related scripts for experimentation

# How to run

* Clone the repository

* Install the required Python Modules
```pip3 install -r requirements.txt```

* Retrieve the submodules
```git submodule update --init --recursive```

* Run and generate the results for the parlaylib benchmarks
```cd <repo_root>/eval/benchmarks; make gen_graphs```

* <repo_root>/eval/benchmarks/result directory will contain the generated raw result files
* <repo_root>/eval/benchmarks/summary_plots directory will contain the generated plots and tables:
    * overhead_plot.png is the overhead plot
    * speedup_subplots.png contains the generated speedup subplots
    * parlay_table_1_40_80.txt is the table with the results for P = 1,40,80
    * parlay_table_all.txt is the table with the results for all P i.e. 1,10,20,30,40,50,60,70,80

* Run and generate the results for the pbbs benchmarks
```cd <repo_root>/pbbs/scripts; nohup ./main_script.sh &```

* <repo_root>/pbbs/result directory will contain the generated results, tables and plot:
    * run_unaug.out and run_aug.out are the logs from the unaugmented and augmented runs
    * unaug.json and aug.json are the processor-wise timing results for each benchmark
    * pbbs_table_1_40_80.txt is the table with the results for P = 1,40,80
    * pbbs_table_all.txt is the table with the results for all P i.e. 1,10,20,30,40,50,60,70,80
    * combined_table_all.txt is the concatended results table with both parlay and pbbs benchmarks
    * overhead_plot.pdf is the overhead plot (in the same format as Figure 12 of the paper)


Note: In order to generate the combined_table_all.txt and the overhead_plot.pdf (similar to Figure 12 of the paper), the expected order is to first generate the parlay benchmark results and then the pbbs benchmark results