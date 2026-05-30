import sys
import re
from collections import defaultdict

# Regex to extract Threads and all metrics key:value pairs
pattern = re.compile(r"Threads:(\d+),(.+)")

# Data structure:
# { threads: { metric_name: cumulative_sum, ..., "count": int } }
data = defaultdict(lambda: defaultdict(float))

for line in sys.stdin:
    line = line.strip()
    match = pattern.search(line)
    if match:
        threads = int(match.group(1))
        metrics_str = match.group(2)

        # Split metrics by comma, then split each key:value pair
        metrics = metrics_str.split(',')

        # Initialize count for this threads group if not present
        if data[threads]["count"] == 0:
            data[threads]["count"] = 0
        data[threads]["count"] += 1

        for metric in metrics:
            if ':' not in metric:
                continue
            key, value = metric.split(':', 1)
            try:
                val_float = float(value)
                data[threads][key] += val_float
            except ValueError:
                # Skip if value is not a number
                pass

# Calculate and print average metrics for each thread count
for threads in sorted(data.keys()):
    count = data[threads].pop("count")
    averages = {k: v / count for k, v in data[threads].items()}
    avg_metrics_str = ', '.join(f"{k}: {v:.2f}" for k, v in averages.items())
    print(f"Threads: {threads}, {avg_metrics_str}")
