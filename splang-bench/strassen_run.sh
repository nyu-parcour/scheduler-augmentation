echo 'size,threads,Observed HWM,S,Predicted,R_inf' > strassen_result.csv
PARLAY_NUM_THREADS=1 ./strassen --size 8192 >> strassen_result.csv &
PARLAY_NUM_THREADS=4 ./strassen --size 8192 >> strassen_result.csv &
PARLAY_NUM_THREADS=8 ./strassen --size 8192 >> strassen_result.csv &
PARLAY_NUM_THREADS=16 ./strassen --size 8192 >> strassen_result.csv &
PARLAY_NUM_THREADS=32 ./strassen --size 8192 >> strassen_result.csv &
PARLAY_NUM_THREADS=64 ./strassen --size 8192 >> strassen_result.csv &
wait
PARLAY_NUM_THREADS=128 ./strassen --size 8192 >> strassen_result.csv
sort -t',' -k2,2n strassen_result.csv -o strassen_result.csv
