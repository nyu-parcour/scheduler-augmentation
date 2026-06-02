import os
import matplotlib.pyplot as plt
import sys
import itertools

ALIAS_MAPPING = {
    "graph/low_diameter_decomposition": "ldd",
    "graph/triangle_count": "triCnt",
    "numerical/karatsuba": "karatsuba",
    "numerical/mcss": "mcss",
    "numerical/primes": "primes",
    "sorting/mergesort": "mergesort",
    "sorting/quicksort": "quicksort",
    "string/huffman_tree": "huffman",
}

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
    table_data = []
    
    col_widths = {
        "benchmark": len("Benchmark"),
        "alias": len("Alias"),
        "input": len("Input"),
        "procs": len("Procs")
    }
        
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
        
        parts = bench.split('_', 1)
        category = parts[0]
        benchmark = parts[1] if len(parts) > 1 else "unknown"
        
        full_name = f"{category}/{benchmark}"
        alias = ALIAS_MAPPING.get(full_name, benchmark)
        input_name = ""
        
        col_widths["benchmark"] = max(col_widths["benchmark"], len(full_name))
        col_widths["alias"] = max(col_widths["alias"], len(alias))
        col_widths["input"] = max(col_widths["input"], len(input_name))

        for pc in common_threads:
            unaug_sec = unaug_times[pc] / 1e9  # Convert to seconds
            aug_sec = aug_times[pc] / 1e9      # Convert to seconds
            
            ratio = aug_sec / unaug_sec if unaug_sec != 0 else float('inf')
            perc_diff = ((aug_sec - unaug_sec) / unaug_sec) * 100 if unaug_sec != 0 else float('inf')
            
            col_widths["procs"] = max(col_widths["procs"], len(str(pc)))
            
            table_data.append({
                "benchmark": full_name,
                "alias": alias,
                "input": input_name,
                "procs": pc,
                "unaug": unaug_sec,
                "aug": aug_sec,
                "ratio": ratio,
                "diff": perc_diff
            })

    # Print Table Header
    header = (
        f"{'Benchmark':<{col_widths['benchmark']}} | "
        f"{'Alias':<{col_widths['alias']}} | "
        f"{'Input':<{col_widths['input']}} | "
        f"{'Procs':>{col_widths['procs']}} | "
        f"{'Unaug Time':>12} | "
        f"{'Aug Time':>12} | "
        f"{'Ratio':>8} | "
        f"{'% Diff':>9}"
    )
    print(header)
    print("-" * len(header))

    # Print Table Rows
    for row in table_data:
        row_str = (
            f"{row['benchmark']:<{col_widths['benchmark']}} | "
            f"{row['alias']:<{col_widths['alias']}} | "
            f"{row['input']:<{col_widths['input']}} | "
            f"{row['procs']:>{col_widths['procs']}} | "
            f"{row['unaug']:>12.4f} | "
            f"{row['aug']:>12.4f} | "
            f"{row['ratio']:>8.3f} | "
            f"{row['diff']:>+9.2f}%"
        )
        print(row_str)

    return table_data

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python script.py <results_dir> <output_dir>")
        sys.exit(1)
        
    result_root_path = sys.argv[1]
    output_dir = sys.argv[2]
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    data = process_benchmarks(result_root_path)