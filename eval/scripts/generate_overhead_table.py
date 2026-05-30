from collections import defaultdict
import os
import sys

def extract_p_thread_time(filename, p):
    if not os.path.isfile(filename):
        return None
    with open(filename, 'r') as f:
        for line in f:
            if line.startswith("Thread"):
                parts = line.strip().split(',')
                thread_count = parts[0].split()[1]
                if int(thread_count) == p and parts[1].split()[0] == "Time:":
                    return float(parts[1].split()[1])
    return None

def process_benchmarks(root, thread_ps=(1, 32)):
    results = defaultdict(list)
    for bench in sorted(os.listdir(root)):
        bench_path = os.path.join(root, bench)
        unaug_file = bench_path
        aug_file = bench_path + ".aug"
        times = {}
        for p in thread_ps:
            unaug_time = extract_p_thread_time(unaug_file, p)
            aug_time = extract_p_thread_time(aug_file, p)
            if unaug_time is None or aug_time is None:
                times[p] = None
                continue
            ratio = aug_time / unaug_time
            percentage_diff = ((aug_time - unaug_time) / unaug_time) * 100
            times[p] = (unaug_time, aug_time, ratio, percentage_diff)
        if all(times[p] for p in thread_ps):
            category, benchmark = bench.split('_', 1)
            results[category].append({
                "benchmark": benchmark,
                "p1": times[1],
                "p40": times[40],
                "p80": times[80],
            })
    return results

root_result_path = sys.argv[1]
results_by_category = process_benchmarks(root_result_path, thread_ps=(1,40,80))

headers = [
    "Category", "Benchmark",
    "UnAug_1 (s)", "Aug_1 (s)", "Ratio_1 (Aug/UnAug, %diff)",
    "UnAug_40 (s)", "Aug_40 (s)", "Ratio_40 (Aug/UnAug, %diff)",
    "UnAug_80 (s)", "Aug_80 (s)", "Ratio_80 (Aug/UnAug, %diff)"
]

all_rows = []
for category, benchmarks in results_by_category.items():
    for r in benchmarks:
        unaug1, aug1, ratio1, diff1 = r['p1']
        unaug40, aug40, ratio40, diff40 = r['p40']
        unaug80, aug80, ratio80, diff80 = r['p80']
        unaug1_s = f"{unaug1/1e9:.4f}"
        aug1_s = f"{aug1/1e9:.4f}"
        ratio1_str = f"{ratio1:.4f} ({diff1:+.2f}%)"
        unaug40_s = f"{unaug40/1e9:.4f}"
        aug40_s = f"{aug40/1e9:.4f}"
        ratio40_str = f"{ratio40:.4f} ({diff40:+.2f}%)"
        unaug80_s = f"{unaug80/1e9:.4f}"
        aug80_s = f"{aug80/1e9:.4f}"
        ratio80_str = f"{ratio80:.4f} ({diff80:+.2f}%)"
        all_rows.append([category, r['benchmark'], unaug1_s, aug1_s, ratio1_str, unaug40_s, aug40_s, ratio40_str, unaug80_s, aug80_s, ratio80_str])

cols = list(zip(*([headers] + all_rows)))
col_widths = [max(len(str(cell)) for cell in col) for col in cols]
row_format = " | ".join([f"{{:<{w}}}" for w in col_widths])

print(row_format.format(*headers))
print("-|-".join(['-'*w for w in col_widths]))
for row in all_rows:
    print(row_format.format(*row))
