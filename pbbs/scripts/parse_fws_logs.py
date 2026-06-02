#!/usr/bin/env python3

import os
import json
import re
from collections import defaultdict

# The list of benchmarks to process.
tests = [
    ["integerSort/parallelRadixSort", True, 0],
    ["comparisonSort/sampleSort", True, 0],
    ["removeDuplicates/parlayhash", True, 0],
    ["histogram/parallel", True, 0],
    ["wordCounts/histogram", True, 0],
    ["invertedIndex/parallel", True, 0],
    ["suffixArray/parallelRange", True, 0],
    ["longestRepeatedSubstring/doubling", True, 0],
    ["classify/decisionTree", True, 0],
    ["minSpanningForest/parallelFilterKruskal", True, 0],
    ["spanningForest/ndST", True, 0],
    ["breadthFirstSearch/backForwardBFS", True, 0],
    ["maximalMatching/incrementalMatching", True, 0],
    ["maximalIndependentSet/incrementalMIS", True, 0],
    ["nearestNeighbors/octTree", True, 0],
    ["rayCast/kdTree", True, 0],
    ["convexHull/quickHull", True, 0],
    ["delaunayTriangulation/incrementalDelaunay", True, 0],
    ["delaunayRefine/incrementalRefine", True, 0],
    ["rangeQuery2d/parallelPlaneSweep", True, 0],
    ["nBody/parallelCK", True, 0],
]

# Dataset mapping for JSON structure
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

def parse_log_line(line):
    """
    Extracts Threads, Forks, Work, and Span from a log line.
    Example line: ... [info] Threads:1,Forks:97130,Work:1570004840,Span:610506641
    """
    pattern = r"Threads:(\d+),Forks:(\d+),Work:(\d+),Span:(\d+)"
    match = re.search(pattern, line)
    if match:
        return {
            "threads": int(match.group(1)),
            "forks": int(match.group(2)),
            "work": int(match.group(3)),
            "span": int(match.group(4))
        }
    return None

def process_vertex_logs():
    all_results = {}
    
    script_dir = os.path.dirname(os.path.abspath(__file__))    
    benchmarks_base_dir = os.path.abspath(os.path.join(script_dir, "..", "benchmarks"))

    for test_config in tests:
        test_name = test_config[0]
        
        if test_name not in DATASET_MAPPING:
            continue
        
        dataset_name = DATASET_MAPPING[test_name]        
        file_path = os.path.join(benchmarks_base_dir, test_name, "logs_vertex.txt")

        if not os.path.exists(file_path):
            print(f"Warning: File not found {file_path}")
            continue

        # Group data points by thread count
        thread_groups = defaultdict(list)
        
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    data = parse_log_line(line)
                    if data:
                        thread_groups[data['threads']].append(data)

            if not thread_groups:
                continue

            all_results[test_name] = {}

            # Calculate averages for each thread count
            for threads in sorted(thread_groups.keys()):
                group = thread_groups[threads]
                count = len(group)
                
                avg_forks = sum(d['forks'] for d in group) / count
                avg_work = sum(d['work'] for d in group) / count
                avg_span = sum(d['span'] for d in group) / count

                thread_key = str(threads)
                all_results[test_name][thread_key] = {
                    dataset_name: {
                        "avg_forks": round(avg_forks, 2),
                        "avg_work": round(avg_work, 2),
                        "avg_span": round(avg_span, 2),
                        "sample_count": count
                    }
                }

        except Exception as e:
            print(f"Error processing {test_name}: {e}")

    return all_results

if __name__ == "__main__":
    final_output = process_vertex_logs()
    print(json.dumps(final_output, indent=4))