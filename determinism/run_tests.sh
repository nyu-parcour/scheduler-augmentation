#!/usr/bin/env bash
# Builds and runs the determinism probes, once with deterministic mode off and
# once with PARLAY_DETERMINISTIC=1. Prints the distinct results seen across
# repeated runs as value×count, so one entry means the result was stable.
set -euo pipefail
cd "$(dirname "$0")"
INC=../include
RUNS=${RUNS:-20}
P=${P:-8}
CXX=${CXX:-c++}
for prog in fork_count stacking pair_traits; do
  $CXX -std=c++17 -O2 -pthread -w -I"$INC" "$prog.cpp" -o "$prog"
done

distinct() { sort -n | uniq -c | awk '{printf "%s×%s  ", $2, $1}'; echo; }

echo "== pair traits (A2) =="
./pair_traits

# Each line runs the program with the given environment and prints whether it
# was accepted, or the error it was rejected with. The granularity is only
# read in deterministic mode, so a bad one is ignored when the flag is off.
echo "== flag parsing =="
G=PARLAY_DETERMINISTIC_GRANULARITY
for envs in "PARLAY_DETERMINISTIC=0" "PARLAY_DETERMINISTIC=1" "PARLAY_DETERMINISTIC=yes" \
            "PARLAY_DETERMINISTIC=1 $G=1025" "PARLAY_DETERMINISTIC=1 $G=0" \
            "PARLAY_DETERMINISTIC=1 $G=abc" "PARLAY_DETERMINISTIC=1 $G=-5" \
            "PARLAY_DETERMINISTIC=1 $G=12x" "PARLAY_DETERMINISTIC=1 $G=" \
            "PARLAY_DETERMINISTIC=1 $G=99999999999999999999999" \
            "$G=0" "PARLAY_DETERMINISTIC=0 $G=abc"; do
  printf "%-74s " "$envs"
  # shellcheck disable=SC2086  # envs is a list of NAME=value words
  if out=$(env PARLAY_NUM_THREADS=$P $envs ./fork_count pfor_gran1 1000 2>&1); then
    echo "accepted"
  else
    echo "rejected: $(grep -o 'PARLAY_DETERMINISTIC[A-Z_]* must.*' <<<"$out" || echo "$out")"
  fi
done

probes() {
  echo "== fork counts, P=$P, $RUNS runs each (A1, A2, A3) =="
  for t in "pair_fill 288" "pair_fill 2000" "u64_fill 2000" "pair_tabulate 20000" "u64_tabulate 20000" "pfor_default 100000" "pfor_gran1 1000"; do
    printf "%-22s " "$t"
    for _ in $(seq "$RUNS"); do PARLAY_NUM_THREADS=$P ./fork_count $t; done | distinct
  done

  echo "== pfor_default 100000 and u64_tabulate 20000 across P (A1, D) =="
  for t in "pfor_default 100000" "u64_tabulate 20000"; do
    for p in 1 2 4 8; do
      printf "%-22s P=%-3s " "$t" "$p"
      for _ in $(seq "$RUNS"); do PARLAY_NUM_THREADS=$p ./fork_count $t; done | distinct
    done
  done

  echo "== stacking at joins, P=$P (B1) =="
  for mode in default conservative; do
    for _ in 1 2 3 4 5; do PARLAY_NUM_THREADS=$P ./stacking $mode; done
  done
}

echo
echo "######## deterministic mode off ########"
(unset PARLAY_DETERMINISTIC PARLAY_DETERMINISTIC_GRANULARITY; probes)

echo
echo "######## PARLAY_DETERMINISTIC=1, granularity ${PARLAY_DETERMINISTIC_GRANULARITY:-1 (default)} ########"
(export PARLAY_DETERMINISTIC=1; probes)
