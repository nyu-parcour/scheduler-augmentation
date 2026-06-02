#!/bin/sh

cd ..

rm -rf result
mkdir result
find ./benchmarks -type f \( -name "avg_timing.txt" -o -name "logs_vertex.txt" \) -delete

nohup python3 -u ./runall -force -cham80 > ./result/nohup_unaug.out 2>&1 &

cd scripts

python3 parse_avg_timing.py > ../result/unaug.json

cd ..
find ./benchmarks -type f \( -name "avg_timing.txt" -o -name "logs_vertex.txt" \) -delete
nohup python3 -u ./runall -force -cham80 -aug > ./result/nohup_aug.out 2>&1 &

python3 parse_avg_timing.py > ../result/aug.json


python3 generate_pbbs_comparison_table.py unaug.json aug.json > ../result/pbbs_table_all.txt
python3 generate_pbbs_1_40_80_table.py unaug.json aug.json > ../result/pbbs_table_1_40_80.txt


python3 concat_tables.py ../result/pbbs_table_all.txt ../../eval/summary_plots/parlay_table_all.txt ../result/combined_table_all.txt