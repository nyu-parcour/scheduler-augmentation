#!/usr/bin/env bash
# Usage: ./run.sh [bench...]   (default: all benchmarks)
set -e

declare -A SIZE=(
  [ann]=10000
  [karatsuba]=65536
  [mergefeyn]=14
  [nqueens]=14
  [strassen]=4096
)

benches=("$@")
[ ${#benches[@]} -eq 0 ] && benches=(ann karatsuba mergefeyn nqueens strassen)

for bench in "${benches[@]}"; do
  size=${SIZE[$bench]}
  if [ -z "$size" ]; then
    echo "unknown benchmark: $bench" >&2
    exit 1
  fi
  out=${bench}-result.csv
  echo 'size,threads,Observed HWM,S,Predicted,R_inf,P*R1star' > "$out"
  for t in 1 4 8 16 32 64; do
    PARLAY_NUM_THREADS=$t ./$bench --size $size >> "$out" &
  done
  wait
  PARLAY_NUM_THREADS=128 ./$bench --size $size >> "$out"
  sort -t',' -k2,2n "$out" -o "$out"
done
