import matplotlib.pyplot as plt
import numpy as np

def parse_benchmark_file(filepath):
    """Parses the text file into a dictionary structure."""
    data = {}
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or "Benchmark" in line or "---" in line:
                continue
            parts = [p.strip() for p in line.split('|')]
            
            # The table format has 8 columns: 
            # [0] Benchmark | [1] Alias | [2] Input | [3] Procs | [4] Unaug Time | [5] Aug Time | [6] Ratio | [7] % Diff
            if len(parts) < 8:
                continue
            
            alias = parts[1]
            try:
                proc = int(parts[3])
                unaug = float(parts[4])
                aug = float(parts[5])
                ratio = float(parts[6])
            except ValueError:
                continue

            if alias not in data:
                data[alias] = {'Proc': [], 'Ratio': []}
            
            data[alias]['Proc'].append(proc)
            data[alias]['Ratio'].append(ratio)
    return data

def generate_plots(data):
    all_benchmarks = list(data.keys())
    
    row3_cell0 = ['rq2d', 'rq2d_ours']
    row3_rest = ['ldd', 'triCnt', 'karatsuba', 'mcss', 'primes', 'mergesort', 'quicksort', 'huffman']
    
    row3_cell0 = [b for b in row3_cell0 if b in data]
    row3_rest = [b for b in row3_rest if b in data]
    row3_groups = [row3_cell0] + [row3_rest[i:i+2] for i in range(0, len(row3_rest), 2)]

    used = set(row3_cell0 + row3_rest)
    remaining = [b for b in all_benchmarks if b not in used]
    
    top_rows_groups = []
    temp_remaining = list(remaining)
    while len(temp_remaining) > 0 and len(top_rows_groups) < 10:
        group = []
        for _ in range(2):
            if temp_remaining:
                group.append(temp_remaining.pop(0))
        top_rows_groups.append(group)

    grid_groups = top_rows_groups + row3_groups

    fig, axes = plt.subplots(3, 5, figsize=(22, 12), sharex='col')
    plt.subplots_adjust(hspace=0.08, wspace=0.15)

    styles = [
        {'color': '#1f77b4', 'marker': 'o', 'markersize': 7, 'linewidth': 1.5, 'zorder': 3}, 
        {'color': "#4cd00a", 'marker': 's', 'markersize': 7, 'linewidth': 1.5, 'linestyle': '--', 'zorder': 2}
    ]

    for row in range(3):
        row_indices = range(row * 5, (row + 1) * 5)
        row_values = []
        for idx in row_indices:
            if idx < len(grid_groups):
                for b_name in grid_groups[idx]:
                    row_values.extend(data[b_name]['Ratio'])
        
        if row_values:
            max_val, min_val = max(row_values), min(row_values)
            max_y = max(max_val, 1.1)
            min_y = min(min_val, 0.9)
            padding = (max_y - min_y) * 0.2
            y_lims = (min_y - padding, max_y + padding)
        else:
            y_lims = (0.8, 1.2)

        for col in range(5):
            ax = axes[row, col]
            group_idx = row * 5 + col
            
            ideal_line = ax.axhline(y=1.0, color='red', linestyle=':', 
                                   linewidth=1.2, alpha=0.6, zorder=1, label="Ideal Overhead = 1")

            benchmark_handles = []
            if group_idx < len(grid_groups):
                bench_names = grid_groups[group_idx]
                for i, name in enumerate(bench_names):
                    line, = ax.plot(data[name]['Proc'], data[name]['Ratio'], 
                                   label=name, **styles[i % 2])
                    benchmark_handles.append(line)
                
                ax.set_ylim(y_lims)
                ax.grid(True, linestyle='-', alpha=0.1, zorder=0)
                
                if row == 0 and col == 0:
                    leg1 = ax.legend(handles=benchmark_handles, labels=[h.get_label() for h in benchmark_handles],
                                     fontsize=15.5, loc='upper right', framealpha=0.9)
                    ax.add_artist(leg1)
                    
                    ax.legend(handles=[ideal_line], labels=["Ideal Overhead = 1"],
                              fontsize=15.5, loc='lower right', framealpha=0.9)
                elif (row == 0 and col == 4) or (row == 0 and col == 3):
                    ax.legend(handles=benchmark_handles, labels=[h.get_label() for h in benchmark_handles],
                    fontsize=15.5, loc='lower right', framealpha=0.9)

                elif (row == 2 and col == 1):
                    leg1 = ax.legend(handles=benchmark_handles[0:1], labels=[h.get_label() for h in benchmark_handles[0:1]],
                            fontsize=15.5, loc='lower right', framealpha=0.9)
                    ax.add_artist(leg1)
                    ax.legend(handles=benchmark_handles[1:2], labels=[h.get_label() for h in benchmark_handles[1:2]],
                    fontsize=15.5, loc='upper right', framealpha=0.9)
                
                elif (row == 2 and col == 0) or (row == 1 and col == 0):
                    leg1 = ax.legend(handles=benchmark_handles[0:1], labels=[h.get_label() for h in benchmark_handles[0:1]],
                              fontsize=15.5, loc='upper right', framealpha=0.9)
                    ax.add_artist(leg1)
                    ax.legend(handles=benchmark_handles[1:2], labels=[h.get_label() for h in benchmark_handles[1:2]],
                    fontsize=15.5, loc='lower right', framealpha=0.9)

                elif row == 1:
                    ax.legend(handles=benchmark_handles, labels=[h.get_label() for h in benchmark_handles],
                            fontsize=15.5, loc='lower right', framealpha=0.9)
                else:
                    ax.legend(handles=benchmark_handles, labels=[h.get_label() for h in benchmark_handles],
                              fontsize=15.5, loc='best', framealpha=0.9)
            
            if col == 0:
                ax.set_ylabel('Ratio (Aug/Unaug)', fontsize=16, fontweight='bold')
            else:
                ax.tick_params(labelleft=False)

            if row == 2 and col == 0:
                x_label = 'Processor Count (P)'
                ax.set_xlabel(x_label, fontsize=16, fontweight='bold')

    plt.savefig('../result/overhead_plot.pdf', bbox_inches='tight', dpi=300)

if __name__ == "__main__":
    file_path = '../result/combined_table_all.txt' 
    try:
        benchmark_data = parse_benchmark_file(file_path)
        generate_plots(benchmark_data)
    except Exception as e:
        import traceback
        traceback.print_exc()