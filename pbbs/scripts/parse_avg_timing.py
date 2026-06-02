#!/usr/bin/env python3

import os
import json

# The list of uncommented tests to process.
tests = [
    ["integerSort/parallelRadixSort",True,0],
    # ["integerSort/serialRadixSort",False,0],

    ["comparisonSort/sampleSort",True,0],
    # ["comparisonSort/quickSort",True,1],
    # ["comparisonSort/mergeSort",True,1],
    # ["comparisonSort/stableSampleSort",True,1],
    # ["comparisonSort/serialSort",False,0],
    # ["comparisonSort/ips4o",True,1],

    # ["removeDuplicates/serial_hash", False,0],
    # ["removeDuplicates/serial_sort", False,1],
    ["removeDuplicates/parlayhash", True,0],

    # ["histogram/sequential",False,0],
    ["histogram/parallel",True,0],
    
    ["wordCounts/histogram",True,0],
    # ["wordCounts/histogramStar",True],
    # ["wordCounts/serial",False,0],

    # ["invertedIndex/sequential", False,0],
    ["invertedIndex/parallel", True,0],
    
    # ["suffixArray/parallelKS",True,1],
    ["suffixArray/parallelRange",True,0],
    # ["suffixArray/serialDivsufsort",False,0],

    ["longestRepeatedSubstring/doubling",True,0],

    ["classify/decisionTree", True,0],

    # ["minSpanningForest/parallelKruskal",True],
    ["minSpanningForest/parallelFilterKruskal",True,0],
    # ["minSpanningForest/serialMST",False,0],

    # ["spanningForest/incrementalST",True,1],
    ["spanningForest/ndST",True,0],
    # ["spanningForest/serialST",False,0],

    # ["breadthFirstSearch/simpleBFS",True,1],
    ["breadthFirstSearch/backForwardBFS",True,0],
    # ["breadthFirstSearch/deterministicBFS",True,1],
    # ["breadthFirstSearch/serialBFS",False,0],

    # ["maximalMatching/serialMatching",False,0],
    ["maximalMatching/incrementalMatching",True,0],

    ["maximalIndependentSet/incrementalMIS",True,0],
    # ["maximalIndependentSet/ndMIS",True,1],
    # ["maximalIndependentSet/serialMIS",False,0],

    ["nearestNeighbors/octTree",True,0],

    ["rayCast/kdTree",True,0],

    ["convexHull/quickHull",True,0],
    # ["convexHull/serialHull",False,0],

    ["delaunayTriangulation/incrementalDelaunay",True,0],

    ["delaunayRefine/incrementalRefine",True,0],
    
    ["rangeQuery2d/parallelPlaneSweep",True,0],
    # ["rangeQuery2d/serial",False,0],

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


def process_benchmarks():
    """
    Reads timing data from subdirectories and returns a JSON object.
    """
    all_results = {}
    
    script_dir = os.path.dirname(os.path.abspath(__file__))    
    benchmarks_base_dir = os.path.abspath(os.path.join(script_dir, "..", "benchmarks"))

    for test_config in tests:
        test_name = test_config[0]
        
        if test_name not in DATASET_MAPPING:
            print(f"Warning: No dataset mapping found for '{test_name}'. Skipping.")
            continue
        
        dataset_name = DATASET_MAPPING[test_name]
        file_path = os.path.join(benchmarks_base_dir, test_name, "avg_timing.txt")

        try:
            with open(file_path, 'r') as f:
                timings = [line.strip() for line in f.readlines()]

            if len(timings) != len(THREAD_COUNTS):
                print(f"Warning: '{file_path}' did not contain {len(THREAD_COUNTS)} lines (found {len(timings)}). Skipping.")
                continue

            all_results[test_name] = {}
            
            for i, thread_count in enumerate(THREAD_COUNTS):
                # Convert nanoseconds to seconds 
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