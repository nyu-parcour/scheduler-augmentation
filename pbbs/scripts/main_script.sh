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


python3 generate_pbbs_comparison_table.py unaug.json aug.json
python3 generate_pbbs_1_40_80_table.py unaug.json aug.json