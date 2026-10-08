#!/usr/bin/env bash
# Usage: ./kclique_run.sh <graph.adj> <k> [--fused-base] [--orient-inside]
# Writes kclique_<graph>_k<k>[_fused][_inside]_result.csv; the CSV's size
# column holds k.
set -e

if [ $# -lt 2 ]; then
  echo "usage: $0 <graph.adj> <k> [--fused-base] [--orient-inside]" >&2
  exit 2
fi
graph=$1
k=$2
shift 2

fused=
inside=
for f in "$@"; do
  case $f in
    --fused-base) fused=_fused ;;
    --orient-inside) inside=_inside ;;
    *) echo "unknown flag: $f" >&2; exit 1 ;;
  esac
done

out=kclique_$(basename "$graph" .adj)_k${k}${fused}${inside}_result.csv
echo 'size,threads,Observed HWM,S,Predicted,R_inf' > "$out"
for t in 1 4 8 16 32 64; do
  PARLAY_NUM_THREADS=$t ./kclique --k "$k" "$@" "$graph" >> "$out" &
done
wait
PARLAY_NUM_THREADS=128 ./kclique --k "$k" "$@" "$graph" >> "$out"
sort -t',' -k2,2n "$out" -o "$out"
