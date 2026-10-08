#!/usr/bin/env bash
# Usage: ./kclique_validate.sh <graph.adj> <k>
#
# For each of the four flag combinations, runs kclique at every thread count
# and prints PASS or FAIL with the reasons:
#   1. the count equals the gbbs reference count;
#   2. count, S, R1star and Rinf are identical across thread counts (R1star is
#      recovered as (Predicted - S) / P); only the observed HWM may vary;
#   3. S == 0 without --orient-inside (with it, S is only printed);
#   4. R1star <= (k-2)(Delta+ + 1), or (k-3)(Delta+ + 1) with --fused-base.
#
# Reference counts live in kclique_expected.csv (graph,k,count,gbbs_commit).
# gbbs is called only to add a missing entry, as
#   Clique_main -s -k K --directType DEGREE --parallelType VERT <graph>
# from a checkout at $GBBS (default ~/gbbs) built, unmodified, with
#   git clone https://github.com/ParAlg/gbbs.git ~/gbbs
#   git -C ~/gbbs checkout bd48872385ad907010417bfa939f4fca04e563b8
#   cd ~/gbbs && USE_BAZEL_VERSION=7.4.1 bazel build //benchmarks/CliqueCounting:Clique_main
# (gbbs no longer builds with make, and its BUILD files predate Bazel 8.)
#
# Graphs are keyed by file name without .adj, and made with pbbs's tools
# (G=../pbbs/testData/graphData):
#   com-dblp  SNAP com-dblp.ungraph.txt.gz, '#' lines stripped, "EdgeArray"
#             prepended, then $G/edgeArrayToAdj -o com-dblp.adj <edges>
#   rmat17    $G/rMatGraph -j 131072 rmat17.adj   (a=.5 b=c=.1 m=10n seed 1)
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <graph.adj> <k>" >&2
  exit 2
fi
graph=$1
k=$2
name=$(basename "$graph" .adj)
here=$(cd "$(dirname "$0")" && pwd)
expected=$here/kclique_expected.csv
threads=(1 4 8 16 32 64 128)

GBBS=${GBBS:-$HOME/gbbs}
gbbs_commit=bd48872385ad907010417bfa939f4fca04e563b8
gbbs_bin=$GBBS/bazel-bin/benchmarks/CliqueCounting/Clique_main

[ -f "$expected" ] || echo 'graph,k,count,gbbs_commit' > "$expected"

ref=$(awk -F, -v g="$name" -v k="$k" '$1 == g && $2 == k { print $3 }' "$expected")
if [ -z "$ref" ]; then
  head=$(git -C "$GBBS" rev-parse HEAD)
  if [ "$head" != "$gbbs_commit" ]; then
    echo "error: $GBBS is at $head, expected $gbbs_commit" >&2
    exit 1
  fi
  if [ ! -x "$gbbs_bin" ]; then
    (cd "$GBBS" && USE_BAZEL_VERSION=7.4.1 bazel build //benchmarks/CliqueCounting:Clique_main)
  fi
  echo "gbbs: computing the reference count for $name, k = $k"
  ref=$("$gbbs_bin" -s -k "$k" --directType DEGREE --parallelType VERT "$graph" |
        awk -v k="$k" '!found && $0 ~ "^### Num " k " cliques = " { print $NF; found = 1 }')
  if [ -z "$ref" ]; then
    echo "error: gbbs printed no count" >&2
    exit 1
  fi
  echo "$name,$k,$ref,$gbbs_commit" >> "$expected"
fi
echo "$name k=$k: reference count $ref"

status=0
for flags in "" "--fused-base" "--orient-inside" "--fused-base --orient-inside"; do
  label=${flags:-faithful}
  fails=()
  first=
  for t in "${threads[@]}"; do
    # stdout: size,threads,Observed HWM,S,Predicted,R_inf
    # stderr: count C n N m M dplus D
    err=$(mktemp)
    if ! row=$(PARLAY_NUM_THREADS=$t "$here/kclique" --k "$k" $flags "$graph" 2> "$err"); then
      fails+=("P=$t: kclique failed: $(tail -1 "$err")")
      rm -f "$err"
      continue
    fi
    IFS=, read -r _ p _ s pred rinf <<< "$row"
    read -r _ count _ _ _ _ _ dplus < "$err"
    rm -f "$err"

    if [ $(( (pred - s) % p )) -ne 0 ]; then
      fails+=("P=$p: Predicted - S = $((pred - s)) is not a multiple of P")
    fi
    r1=$(( (pred - s) / p ))
    if [ "$count" != "$ref" ]; then
      fails+=("P=$p: count $count != reference $ref")
    fi
    cur=("$count" "$s" "$r1" "$rinf" "$dplus")
    if [ -z "$first" ]; then
      first=1
      first_p=$p
      read -r count1 s1 r11 rinf1 dplus1 <<< "${cur[*]}"
    elif [ "${cur[*]}" != "$count1 $s1 $r11 $rinf1 $dplus1" ]; then
      fails+=("P=$p: count S R1* Rinf dplus = ${cur[*]}, at P=$first_p $count1 $s1 $r11 $rinf1 $dplus1")
    fi
  done

  if [ -z "$first" ]; then
    status=1
    echo "FAIL [$label] no run succeeded"
    for f in "${fails[@]}"; do echo "    $f"; done
    continue
  fi
  if [[ $flags == *--fused-base* ]]; then levels=$((k - 3)); else levels=$((k - 2)); fi
  bound=$((levels * (dplus1 + 1)))
  if [[ $flags != *--orient-inside* ]] && [ "$s1" -ne 0 ]; then
    fails+=("S = $s1, expected 0")
  fi
  if [ "$r11" -gt "$bound" ]; then
    fails+=("R1* = $r11 > $levels*(dplus+1) = $bound")
  fi

  summary="S=$s1 R1*=$r11 (bound $bound) Rinf=$rinf1 dplus=$dplus1"
  if [ ${#fails[@]} -eq 0 ]; then
    echo "PASS [$label] $summary"
  else
    status=1
    echo "FAIL [$label] $summary"
    for f in "${fails[@]}"; do echo "    $f"; done
  fi
done
exit $status
