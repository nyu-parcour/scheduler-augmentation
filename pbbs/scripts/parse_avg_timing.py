#!/usr/bin/env python3

import os
import json

tests = [
    ["integerSort/parallelRadixSort",True,0],
    ["comparisonSort/sampleSort",True,0],
    ["removeDuplicates/parlayhash", True,0],
    ["histogram/parallel",True,0],
    ["wordCounts/histogram",True,0],
    ["invertedIndex/parallel", True,0],
    ["suffixArray/parallelRange",True,0],
    ["longestRepeatedSubstring/doubling",True,0],
    ["classify/decisionTree", True,0],
    ["minSpanningForest/parallelFilterKruskal",True,0],
    ["spanningForest/ndST",True,0],
    ["breadthFirstSearch/backForwardBFS",True,0],
    ["maximalMatching/incrementalMatching",True,0],
    ["maximalIndependentSet/incrementalMIS",True,0],
    ["nearestNeighbors/octTree",True,0],
    ["rayCast/kdTree",True,0],
    ["convexHull/quickHull",True,0],
    ["delaunayTriangulation/incrementalDelaunay",True,0],
    ["delaunayRefine/incrementalRefine",True,0],
    ["rangeQuery2d/parallelPlaneSweep",True,0],
    ["nBody/parallelCK",True,0],
]

# The thread counts corresponding to the lines in avg_timing.txt
THREAD_COUNTS = [1, 10, 20, 30, 40, 50, 60, 70, 80]

# Mapping from benchmark name to its dataset name.
DATASET_MAPPING = {
    "integerSort/parallelRadixSort": "randomSeq_100M_int",
    "comparisonSort/sampleSort": "randomSeq_100M_double",
    "removeDuplicates/parlayhash": "randomSeq_100M_int",
    "histogram/parallel": "randomSeq_100M_int",
    "wordCounts/histogram": "wikipedia250M.txt",
    "invertedIndex/parallel": "wikipedia250M.txt",
    "suffixArray/parallelRange": "chr22.dna",
    "longestRepeatedSubstring/doubling": "chr22.dna",
    "classify/decisionTree": "covtype.data",
    "minSpanningForest/parallelFilterKruskal": "rMatGraph_WE_12_16000000",
    "spanningForest/ndST": "rMatGraph_E_12_16000000",
    "breadthFirstSearch/backForwardBFS": "rMatGraph_J_12_16000000",
    "maximalMatching/incrementalMatching": "rMatGraph_E_10_20000000",
    "maximalIndependentSet/incrementalMIS": "rMatGraph_JR_12_16000000",
    "nearestNeighbors/octTree": "2Dkuzmin_10M",
    "rayCast/kdTree": "happyTriangles happyRays",
    "convexHull/quickHull": "2Dkuzmin_100000000",
    "delaunayTriangulation/incrementalDelaunay": "2Dkuzmin_10M",
    "delaunayRefine/incrementalRefine": "2DkuzminDelaunay_5000000",
    "rangeQuery2d/parallelPlaneSweep": "2Dkuzmin_10M",
    "nBody/parallelCK": "3DonSphere_1000000",
}

# Alias Mapping
ALIAS_MAPPING = {
    "integerSort/parallelRadixSort": "intSort",
    "comparisonSort/sampleSort": "cmpSort",
    "removeDuplicates/parlayhash": "rmDup",
    "histogram/parallel": "hist",
    "wordCounts/histogram": "wordCnt",
    "invertedIndex/parallel": "invIdx",
    "suffixArray/parallelRange": "suffArr",
    "longestRepeatedSubstring/doubling": "lrs",
    "classify/decisionTree": "classDT",
    "minSpanningForest/parallelFilterKruskal": "msf",
    "spanningForest/ndST": "sf",
    "breadthFirstSearch/backForwardBFS": "bfs",
    "maximalMatching/incrementalMatching": "maxMatch",
    "maximalIndependentSet/incrementalMIS": "mis",
    "nearestNeighbors/octTree": "nn",
    "rayCast/kdTree": "rayCast",
    "convexHull/quickHull": "cvxHull",
    "delaunayTriangulation/incrementalDelaunay": "delTri",
    "delaunayRefine/incrementalRefine": "delRef",
    "rangeQuery2d/parallelPlaneSweep": "rq2d",
    "rangeQuery2d/parallelPlaneSweep_fixed": "rq2d_ours",
    "nBody/parallelCK": "nBody",
    "graph/low_diameter_decomposition": "ldd",
    "graph/triangle_count": "triCnt",
    "numerical/karatsuba": "karatsuba",
    "numerical/mcss": "mcss",
    "numerical/primes": "primes",
    "sorting/mergesort": "mergesort",
    "sorting/quicksort": "quicksort",
    "string/huffman_tree": "huffman",
}

def process_benchmarks():
    """
    Reads timing data from subdirectories and returns a JSON object 
    containing both the benchmark alias and the full benchmark name.
    """
    all_results = {}
    
    script_dir = os.path.dirname(os.path.abspath(__file__))    
    benchmarks_base_dir = os.path.abspath(os.path.join(script_dir, "..", "benchmarks"))

    for test_config in tests:
        test_name = test_config[0]
        
        if test_name not in DATASET_MAPPING:
            print(f"Warning: No dataset mapping found for '{test_name}'. Skipping.")
            continue
        
        alias_name = ALIAS_MAPPING.get(test_name, test_name)
        dataset_name = DATASET_MAPPING[test_name]
        
        file_path = os.path.join(benchmarks_base_dir, test_name, "avg_timing.txt")

        try:
            with open(file_path, 'r') as f:
                timings = [line.strip() for line in f.readlines()]

            if len(timings) != len(THREAD_COUNTS):
                print(f"Warning: '{file_path}' did not contain {len(THREAD_COUNTS)} lines (found {len(timings)}). Skipping.")
                continue

            all_results[test_name] = {
                "alias": alias_name
            }
            
            for i, thread_count in enumerate(THREAD_COUNTS):
                time_val = round(float(timings[i]) / 1_000_000_000, 6)
                thread_key = str(thread_count)  
                
                all_results[test_name][thread_key] = {
                    dataset_name: time_val
                }

        except FileNotFoundError:
            print(f"Error: Could not find '{file_path}'. Skipping test '{test_name}'.")
        except Exception as e:
            print(f"Error processing '{test_name}': {e}. Skipping.")

    return all_results

if __name__ == "__main__":
    results_json = process_benchmarks()
    print(json.dumps(results_json, indent=4))