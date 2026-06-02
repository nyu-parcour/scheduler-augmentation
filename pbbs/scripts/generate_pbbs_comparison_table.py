import json
import sys

l = []

def generate_comparison_table(unaug_file, aug_file):
    """
    Loads benchmark data from two JSON files, compares the results,
    and prints a neatly aligned table. The table preserves the original
    benchmark order and sorts results by processor count internally.

    Args:
        unaug_file (str): Path to the JSON file with unaugmented results (baseline).
        aug_file (str): Path to the JSON file with augmented results.
    """
    try:
        with open(unaug_file, 'r') as f:
            unaug_data = json.load(f)
        with open(aug_file, 'r') as f:
            aug_data = json.load(f)
    except FileNotFoundError as e:
        print(f"Error: Could not find file {e.filename}")
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Could not parse JSON in a file. Details: {e}")
        sys.exit(1)

    col_widths = {
        "benchmark": len("Benchmark"),
        "alias": len("Alias"),
        "input": len("Input"),
        "procs": len("Procs")
    }

    for benchmark, b_data in unaug_data.items():
        col_widths["benchmark"] = max(col_widths["benchmark"], len(benchmark))
        
        alias = b_data.get("alias", "N/A")
        col_widths["alias"] = max(col_widths["alias"], len(alias))
        
        for key, inputs in b_data.items():
            if not key.isdigit():
                continue
                
            col_widths["procs"] = max(col_widths["procs"], len(key))
            for input_name in inputs:
                col_widths["input"] = max(col_widths["input"], len(input_name))
    
    header = (
        f"{'Benchmark':<{col_widths['benchmark']}} | "
        f"{'Alias':<{col_widths['alias']}} | "
        f"{'Input':<{col_widths['input']}} | "
        f"{'Procs':>{col_widths['procs']}} | "
        f"{'Unaug Time':>12} | "
        f"{'Aug Time':>12} | "
        f"{'Ratio':>8} | "
        f"{'% Diff':>9}"
    )
    print(header)
    print("-" * len(header))

    for benchmark, b_data in unaug_data.items():
        alias = b_data.get("alias", "N/A")
        
        procs_keys = [k for k in b_data.keys() if k.isdigit()]
        sorted_procs = sorted([int(p) for p in procs_keys])

        for procs in sorted_procs:
            procs_str = str(procs)
            inputs = b_data[procs_str]

            for input_name in sorted(inputs.keys()):
                aug_val = aug_data.get(benchmark, {}).get(procs_str, {}).get(input_name)
                if aug_val is None:
                    continue

                unaug_val = inputs[input_name]
                ratio = aug_val / unaug_val if unaug_val != 0 else float('inf')
                pct_diff = ((aug_val - unaug_val) / unaug_val) * 100 if unaug_val != 0 else float('inf')
                
                row = (
                    f"{benchmark:<{col_widths['benchmark']}} | "
                    f"{alias:<{col_widths['alias']}} | "
                    f"{input_name:<{col_widths['input']}} | "
                    f"{procs:>{col_widths['procs']}} | "
                    f"{unaug_val:>12.4f} | "
                    f"{aug_val:>12.4f} | "
                    f"{ratio:>8.3f} | "
                    f"{pct_diff:>+9.2f}%"
                )
                print(row)
                # l.append(pct_diff)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python compare_benchmarks.py <unaugmented_results.json> <augmented_results.json>")
        sys.exit(1)

    unaugmented_file = sys.argv[1]
    augmented_file = sys.argv[2]
    
    generate_comparison_table(unaugmented_file, augmented_file)

    # print("Average % Diff:", sum(l)/len(l) if l else 0)
    # print(l)