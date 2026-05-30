import os
import matplotlib.pyplot as plt
import math
import sys

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
                thread_count = parts[0].split()[1]
                if  parts[1].split()[0] == "Time:":
                    try:
                        p = int(thread_count)
                        t = float(parts[1].split()[1])
                        times[p] = t
                    except Exception:
                        continue
    return times

def process_benchmarks(root):
    data = {}
    for bench in sorted(os.listdir(root)):
        bench_path = os.path.join(root, bench)
        unaug_file = bench_path
        aug_file = bench_path + ".aug"

        unaug_times = extract_thread_times(unaug_file)
        aug_times = extract_thread_times(aug_file)

        common_threads = sorted(set(unaug_times.keys()).intersection(aug_times.keys()))
        if not common_threads:
            continue

        # Normalize by time at one thread
        unaug_1 = unaug_times.get(1)
        aug_1 = aug_times.get(1)
        if not unaug_1 or not aug_1:
            continue

        unaug_norm = [unaug_1 / unaug_times[p] for p in common_threads]  # Unaug speedup normalized to 1 thread
        aug_norm = [unaug_1 / aug_times[p] for p in common_threads]        # Aug speedup normalized to 1 thread

        category, benchmark = bench.split('_', 1)
        if category not in data:
            data[category] = {}
        data[category][benchmark] = (common_threads, unaug_norm, aug_norm)

    return data

def plot_normalized_speedups(data, save_path):
    categories = []
    split_idxs = {}
    for cat in data:
        if cat == "graph" and len(data[cat]) == 4:
            categories.extend([f"graph_1", f"graph_2"])
            split_idxs["graph"] = [list(data[cat].items())[:2], list(data[cat].items())[2:]]
        else:
            categories.append(cat)
        
    n = len(categories)
    cols = math.ceil(math.sqrt(n))
    rows = math.ceil(n / cols)

    fig, axes = plt.subplots(rows, cols, figsize=(5 * max(rows, cols), 5 * max(rows, cols)), sharex=True, sharey=True)
    if n == 1:
        axes = [[axes]]
    elif rows == 1:
        axes = [axes]
    axes = [ax for row in axes for ax in row]

    colors = plt.cm.tab20.colors
    curr_ax = 0

    for cat in categories:
        ax = axes[curr_ax]
        curr_ax += 1
        if cat.startswith("graph_"):
            idx = int(cat.split("_")[1]) - 1
            group = split_idxs["graph"][idx]
            benchmarks = dict(group)
            title = "graph"
        else:
            benchmarks = data[cat]
            title = cat

        color_idx = 0
        all_threads = set()
        for (threads, _, _) in benchmarks.values():
            all_threads.update(threads)
        all_threads = sorted(all_threads)

        for bench, (threads, unaug_norm, aug_norm) in benchmarks.items():
            color = colors[color_idx % len(colors)]
            ax.plot(threads, unaug_norm, color=color, linestyle='-', marker='o', label=f"{bench} Unaug")
            ax.plot(threads, aug_norm, color=color, linestyle=':', marker='x', label=f"{bench} Aug")
            color_idx += 1

        ax.set_title(title, fontsize=12, fontweight='bold')
        ax.set_xlabel('Number of Processors (P)', fontsize=10, labelpad=10)
        ax.set_ylabel('Speedup (T(1)/T(P))', fontsize=10, labelpad=10)
        ax.grid(True, linestyle=':', linewidth=0.7, alpha=0.7)
        ax.set_xticks(all_threads)
        ax.tick_params(axis='x', labelsize=9, labelbottom=True)
        ax.tick_params(axis='y', labelsize=9, labelleft=True)
        ax.legend(fontsize='8', loc='upper left', frameon=False)
        xlim = ax.get_xlim()
        ylim = ax.get_ylim()
        lim_min = min(xlim[0], ylim[0])
        lim_max = max(xlim[1], ylim[1])
        ax.set_xlim(lim_min, lim_max)
        ax.set_ylim(lim_min, lim_max)
        ax.set_aspect('equal', adjustable='box')


    # Hide empty subplots if any
    for j in range(curr_ax, len(axes)):
        fig.delaxes(axes[j])

    plt.tight_layout(pad=3.0)
    plt.subplots_adjust(bottom=0.15, right=0.9)
    plt.savefig(save_path, dpi=300)
    # print(f"Plot saved as {save_path}")
    # plt.show()


result_root_path = sys.argv[1]

data = process_benchmarks(result_root_path)
plot_normalized_speedups(data, sys.argv[2] + "/speedup_subplots.png")
