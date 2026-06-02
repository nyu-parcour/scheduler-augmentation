#!/usr/bin/env python3
import sys

def concatenate_tables(file1, file2, output_file):
    """
    Concatenates two benchmark table text files.
    Keeps the header from the first file and strips the header from the second.
    """
    try:
        with open(output_file, 'w') as outfile:
            
            # 1. Write the entirety of the first file (including its header)
            with open(file1, 'r') as f1:
                for line in f1:
                    outfile.write(line)
            
            # Ensure there is a newline between the files if f1 didn't end with one
            outfile.write("\n")
            
            # 2. Write the second file, skipping its header and divider
            with open(file2, 'r') as f2:
                for line in f2:
                    # Skip the header line and the dashed divider line
                    if "Benchmark" in line or "---" in line:
                        continue
                    # Skip entirely empty lines just to keep the output neat
                    if not line.strip():
                        continue
                        
                    outfile.write(line)
                    
        print(f"Successfully concatenated:\n  1. {file1}\n  2. {file2}\nOutput saved to: {output_file}")
        
    except FileNotFoundError as e:
        print(f"Error: Could not find file {e.filename}")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python3 concat_tables.py <table1.txt> <table2.txt> <output_table.txt>")
        sys.exit(1)

    file1_path = sys.argv[1]
    file2_path = sys.argv[2]
    output_path = sys.argv[3]
    
    concatenate_tables(file1_path, file2_path, output_path)