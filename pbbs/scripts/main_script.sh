#!/usr/bin/env bash

# Fail immediately if any command exits with a non-zero status
set -e

# 1. Resolve Absolute Paths
# Get the absolute path of the directory containing this script (e.g., /path/to/project/scripts)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Define the pbbs root directory (one level up from scripts)
PBBS_ROOT_DIR="$(dirname "$SCRIPT_DIR")"

# Define explicit paths to avoid any relative ambiguity
RESULT_DIR="${PBBS_ROOT_DIR}/result"
BENCHMARKS_DIR="${PBBS_ROOT_DIR}/benchmarks"
EVAL_DIR="$(dirname "$PBBS_ROOT_DIR")/eval" 

export CPATH="$(dirname "$PBBS_ROOT_DIR")/include/spdlog/include:$CPATH"

echo "Initializing benchmark pipeline..."

# 2. Clean up and setup
rm -rf "$RESULT_DIR"
mkdir -p "$RESULT_DIR"
find "$BENCHMARKS_DIR" -type f \( -name "avg_timing.txt" -o -name "logs_vertex.txt" \) -delete

# 3. Run unaugmented benchmarks
echo "Running unaugmented benchmarks..."
python3 -u "$PBBS_ROOT_DIR/runall" -force -cham80 > "$RESULT_DIR/run_unaug.out" 2>&1
python3 "$SCRIPT_DIR/parse_avg_timing.py" > "$RESULT_DIR/unaug.json"

# 4. Clean up before augmented run
find "$BENCHMARKS_DIR" -type f \( -name "avg_timing.txt" -o -name "logs_vertex.txt" \) -delete

# 5. Run augmented benchmarks
echo "Running augmented benchmarks..."
python3 -u "$PBBS_ROOT_DIR/runall" -force -cham80 -aug > "$RESULT_DIR/run_aug.out" 2>&1
python3 "$SCRIPT_DIR/parse_avg_timing.py" > "$RESULT_DIR/aug.json"

# 6. Generate tables
echo "Generating comparison tables..."
python3 "$SCRIPT_DIR/generate_pbbs_comparison_table.py" "$RESULT_DIR/unaug.json" "$RESULT_DIR/aug.json" > "$RESULT_DIR/pbbs_table_all.txt"
python3 "$SCRIPT_DIR/generate_pbbs_1_40_80_table.py" "$RESULT_DIR/unaug.json" "$RESULT_DIR/aug.json" > "$RESULT_DIR/pbbs_table_1_40_80.txt"

# 7. Concatenate and Graph
echo "Concatenating tables and generating graphs..."
python3 "$SCRIPT_DIR/concat_tables.py" "$RESULT_DIR/pbbs_table_all.txt" "$EVAL_DIR/summary_plots/parlay_table_all.txt" "$RESULT_DIR/combined_table_all.txt"

cd "$SCRIPT_DIR" 
python3 generate_overhead_graph.py

echo "Pipeline complete! Results saved in $RESULT_DIR."