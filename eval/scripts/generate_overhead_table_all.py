import os
import matplotlib.pyplot as plt
import sys
import itertools

def extract_thread_times(filename):
    times = {}
    if not os.path.isfile(filename):
        return times
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if line.startswith("Thread"):
                parts = line.strip().split(',')
                try:
                    thread_count = int(parts[0].split()[1])
                    if parts[1].split()[0] == "Time:":
                        t = float(parts[1].split()[1])
                        times[thread_count] = t
                except Exception:
                    continue
    return times

def process_benchmarks(root):
    overhead_data = {}
        
    for bench in sorted(os.listdir(root)):
        if bench.endswith(".aug"):
            continue
            
        bench_path = os.path.join(root, bench)
        unaug_file = bench_path
        aug_file = bench_path + ".aug"
        unaug_times = extract_thread_times(unaug_file)
        aug_times = extract_thread_times(aug_file)

        common_threads = sorted(set(unaug_times.keys()).intersection(aug_times.keys()))
        if not common_threads:
            continue

        overheads = [aug_times[p] / unaug_times[p] for p in common_threads]
        
        parts = bench.split('_', 1)
        category = parts[0]
        benchmark = parts[1] if len(parts) > 1 else "unknown"
        
        # if category not in overhead_data:
        #     overhead_data[category] = {}
        # overhead_data[category][benchmark] = (common_threads, overheads)

        for pc in common_threads:
            perc_diff = ((aug_times[pc] - unaug_times[pc]) / unaug_times[pc]) * 100
            unaug_times[pc] = unaug_times[pc] / 1e9  # Convert to seconds
            aug_times[pc] = aug_times[pc] / 1e9  # Convert to seconds
            print(f"{category}/{benchmark} |   | {pc} | {unaug_times[pc]:.4f} | {aug_times[pc]:.4f} | {overheads[common_threads.index(pc)]:.3f} | {perc_diff:+.2f}%")

    return overhead_data    

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python script.py <results_dir> <output_dir>")
        sys.exit(1)
        
    result_root_path = sys.argv[1]
    output_dir = sys.argv[2]
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    data = process_benchmarks(result_root_path)