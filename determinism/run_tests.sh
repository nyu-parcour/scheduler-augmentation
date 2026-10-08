#!/usr/bin/env bash
# Builds and runs the determinism probes. Prints the distinct results seen
# across repeated runs as value×count, so one entry means the result was stable.
set -euo pipefail
cd "$(dirname "$0")"
INC=../include
RUNS=${RUNS:-20}
P=${P:-8}
CXX=${CXX:-c++}
for prog in fork_count stacking pair_traits; do
  $CXX -std=c++17 -O2 -pthread -I"$INC" "$prog.cpp" -o "$prog" 2>/dev/null
done

distinct() { sort -n | uniq -c | awk '{printf "%s×%s  ", $2, $1}'; echo; }

echo "== pair traits (A2) =="
./pair_traits

echo "== fork counts, P=$P, $RUNS runs each (A1, A2, A3) =="
for t in "pair_fill 288" "pair_fill 2000" "u64_fill 2000" "pair_tabulate 20000" "u64_tabulate 20000" "pfor_default 100000" "pfor_gran1 1000"; do
  printf "%-22s " "$t"
  for _ in $(seq "$RUNS"); do PARLAY_NUM_THREADS=$P ./fork_count $t; done | distinct
done

echo "== pfor_default 100000 across P (A1, D) =="
for p in 1 2 4 8; do
  printf "P=%-3s " "$p"
  for _ in $(seq "$RUNS"); do PARLAY_NUM_THREADS=$p ./fork_count pfor_default 100000; done | distinct
done

echo "== stacking at joins, P=$P (B1) =="
for mode in default conservative; do
  for _ in 1 2 3 4 5; do PARLAY_NUM_THREADS=$P ./stacking $mode; done
done
