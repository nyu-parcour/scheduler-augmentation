#!/usr/bin/env bash
# Usage: ./run.sh [bench...]   (default: all benchmarks)
set -e

declare -A SIZE=(
  [ann]=10000
  [karatsuba]=65536
  [nqueens]=14
  [strassen]=1024
)

benches=("$@")
[ ${#benches[@]} -eq 0 ] && benches=(ann karatsuba nqueens strassen)

for bench in "${benches[@]}"; do
  size=${SIZE[$bench]}
  if [ -z "$size" ]; then
    echo "unknown benchmark: $bench" >&2
    exit 1
  fi
  out=${bench}_result.csv
  echo 'size,threads,Observed HWM,S,Predicted,R_inf' > "$out"
  for t in 1 4 8 16 32 64; do
    PARLAY_NUM_THREADS=$t ./$bench --size $size >> "$out" &
  done
  wait
  PARLAY_NUM_THREADS=128 ./$bench --size $size >> "$out"
  sort -t',' -k2,2n "$out" -o "$out"
done
