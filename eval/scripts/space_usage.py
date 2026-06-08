# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "matplotlib",
# ]
# ///

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import math
import re
import shutil
import subprocess
from pathlib import Path


def format_num(num: int, _):
    if num >= 1_000_000_000:
        return f"{num / 1_000_000_000:.0f}GB"
    elif num >= 1_000_000:
        return f"{num / 1_000_000:.0f}MB"
    elif num >= 1_000:
        return f"{num / 1_000:.0f}KB"
    else:
        return f"{num:.0f}"


def format_x_label(num: int):
    if num >= 1_000_000_000:
        return f"{num / 1_000_000_000:.0f}G"
    elif num >= 1_000_000:
        return f"{num / 1_000_000:.0f}M"
    elif num >= 1_000:
        return f"{num / 1_000:.0f}K"
    else:
        return f"{num:.0f}"


THREADS = 20
LOG_PATTERN = re.compile(r"Threads:(?P<threads>\d+),s1:(?P<s1>\d+),sinf:(?P<sinf>\d+),gross:(?P<gross>\d+)")
SCRIPT_DIR = Path(__file__).resolve().parent
BENCHMARK_DIR = SCRIPT_DIR.parent / "benchmarks"
LOG_FILE = BENCHMARK_DIR / "logs_vertex.txt"
QUICKHULL_BIN = BENCHMARK_DIR / "bin" / "quickhull.aug"
OUTPUT_FILE = SCRIPT_DIR / "space_usage_plot.pdf"


# Data as lists
input_sizes = [
    100000,
    1000000,
    5000000,
    10000000,
    20000000,
    30000000,
    40000000,
    50000000,
]


def run_quickhull(size: int, no_clear: bool) -> int:
    if not QUICKHULL_BIN.exists():
        raise FileNotFoundError(f"Missing benchmark binary: {QUICKHULL_BIN}")

    if LOG_FILE.exists():
        LOG_FILE.unlink()

    command = [str(QUICKHULL_BIN), str(size)]
    if no_clear:
        command.append("--no-clear")

    if shutil.which("numactl"):
        command = ["numactl", "-i", "all", *command]

    env = dict(**subprocess.os.environ, PARLAY_NUM_THREADS=str(THREADS))
    subprocess.run(command, cwd=BENCHMARK_DIR, env=env, check=True, capture_output=True, text=True)

    if not LOG_FILE.exists():
        raise RuntimeError(f"Benchmark completed without producing {LOG_FILE}")

    for line in reversed(LOG_FILE.read_text().splitlines()):
        match = LOG_PATTERN.search(line)
        if match and int(match.group("threads")) == THREADS:
            return int(match.group("sinf"))

    raise RuntimeError(f"Could not find sinf data for thread count {THREADS} in {LOG_FILE}")


def collect_space_usage(no_clear: bool) -> list[int]:
    return [run_quickhull(size, no_clear=no_clear) for size in input_sizes]


s_inf_opt = collect_space_usage(no_clear=False)
s_inf = collect_space_usage(no_clear=True)

# Theoretical calculations using list comprehensions and math module
four_n_log_n = [4 * n * math.log2(n) for n in input_sizes]
twelve_n = [12 * n for n in input_sizes]

# Grouping for plotting
plot_data = [
    (s_inf, r"$S_{\infty}$", "blue", "o", "-"),
    (s_inf_opt, r"$S_{\infty}^{opt}$", "red", "s", "-"),
    (four_n_log_n, r"$4n\log_{2}n$", "green", None, ":"),
    (twelve_n, r"$12n$", "purple", None, "--"),
]

plt.figure(figsize=(12, 8))

# Iterate over plot_data list
for values, label, color, marker, ls in plot_data:
    plt.plot(
        input_sizes,
        values,
        marker=marker,
        markersize=8,
        linestyle=ls,
        color=color,
        label=label,
        linewidth=3,
    )

plt.title("Space profiling vs. manual analysis", fontsize=22)
plt.xlabel("Input size (# of points)", fontsize=22)

# Apply custom formatter to y-axis ticks
formatter = mticker.FuncFormatter(format_num)
plt.gca().yaxis.set_major_formatter(formatter)

# Set y-axis to start from 0
plt.ylim(bottom=0)

# Format x-axis labels
plt.xticks(
    input_sizes,
    labels=[format_x_label(s) for s in input_sizes],
    rotation=45,
    ha="right",
    fontsize=22,
)

plt.yticks(fontsize=22)
plt.grid(True, which="both", ls="--", c="0.7")
plt.legend(fontsize=20)
plt.tight_layout()
plt.savefig(OUTPUT_FILE, format="pdf", bbox_inches="tight")
plt.show()
