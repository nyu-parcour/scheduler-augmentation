#!/usr/bin/env bash
echo 'size,threads,Observed HWM,S,Predicted,R_inf' > nqueens_result.csv
PARLAY_NUM_THREADS=1 ./nqueens --size 14 >> nqueens_result.csv &
PARLAY_NUM_THREADS=4 ./nqueens --size 14 >> nqueens_result.csv &
PARLAY_NUM_THREADS=8 ./nqueens --size 14 >> nqueens_result.csv &
PARLAY_NUM_THREADS=16 ./nqueens --size 14 >> nqueens_result.csv &
PARLAY_NUM_THREADS=32 ./nqueens --size 14 >> nqueens_result.csv &
PARLAY_NUM_THREADS=64 ./nqueens --size 14 >> nqueens_result.csv &
wait
PARLAY_NUM_THREADS=128 ./nqueens --size 14 >> nqueens_result.csv
sort -t',' -k2,2n nqueens_result.csv -o nqueens_result.csv
