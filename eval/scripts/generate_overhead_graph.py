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
        
        if category not in overhead_data:
            overhead_data[category] = {}
        overhead_data[category][benchmark] = (common_threads, overheads)

    return overhead_data

def plot_overhead(overhead_data, save_path):
    plt.figure(figsize=(14, 9))

    colors = plt.cm.tab20.colors
    # List of diverse marker shapes to cycle through
    markers = ['o', 's', 'D', '^', 'v', 'p', '*', 'h', 'X', 'P']
    
    # Create iterators that cycle indefinitely
    color_cycle = itertools.cycle(colors)
    marker_cycle = itertools.cycle(markers)

    all_threads = set()
    for benchmarks in overhead_data.values():
        for _, (threads, _) in benchmarks.items():
            all_threads.update(threads)

    all_threads = sorted(all_threads)

    for category, benchmarks in overhead_data.items():
        for bench, (threads, overheads) in benchmarks.items():
            color = next(color_cycle)
            marker = next(marker_cycle)
            
            plt.scatter(threads, overheads, color=color, marker=marker,
                        label=f"{category}/{bench}", s=60, edgecolors='black', linewidth=0.6, alpha=0.85)

    plt.axhline(y=1, color='red', linestyle='--', linewidth=1.2, label='Overhead = 1')

    plt.xlabel('Number of Processors (P)', fontsize=12, weight='bold')
    plt.ylabel('Overhead (Aug(P) / UnAug(P))', fontsize=12, weight='bold')

    plt.xticks(all_threads, fontsize=12)
    plt.yticks(fontsize=12)

    plt.grid(True, linestyle=':', linewidth=0.8, alpha=0.7)

    plt.legend(bbox_to_anchor=(1.02, 1), loc='upper left', fontsize=10, borderaxespad=0.)

    plt.tight_layout(rect=[0, 0, 0.82, 1])

    plt.savefig(save_path, dpi=300)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python script.py <results_dir> <output_dir>")
        sys.exit(1)
        
    result_root_path = sys.argv[1]
    output_dir = sys.argv[2]
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    data = process_benchmarks(result_root_path)
    plot_overhead(data, os.path.join(output_dir, "overhead_plot.png"))