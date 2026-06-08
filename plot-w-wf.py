import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.colors as colors
import numpy as np
import re
import sys
import argparse

def custom_formatter(x, pos):
    if x <= 100:
        return f'{int(x)}'
    else:
        exponent = int(round(np.log10(x)))
        return f'$10^{{{exponent}}}$'

def create_scatter_plot():
    parser = argparse.ArgumentParser(description='Create scatter plot from log file')
    parser.add_argument('filename', help='Path to the log file')
    parser.add_argument('title', nargs='?', default=None, help='Plot title (optional)')
    parser.add_argument('--no-ylabel', action='store_true', help='Hide y-axis label')
    parser.add_argument('--no-cbar-label', action='store_true', help='Hide colorbar label')
    
    args = parser.parse_args()
    
    filename = args.filename
    plot_title = args.title if args.title else f'All-Subdag Granularity Analysis: {filename}'
    
    phase_vals, x_coords, y_coords, z_values = [], [], [], []
    
    # Updated pattern: captures three integers at the end of the line
    pattern = re.compile(r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$')

    try:
        with open(filename, 'r') as file:
            for line in file:
                match = pattern.search(line.strip())
                if match:
                    phase_val = int(match.group(1))
                    x_val = int(match.group(2))
                    z_val = int(match.group(3)) # The middle value
                    y_val = int(match.group(4))
                    
                    if x_val > 0 and y_val > 0 and z_val > 0:
                        phase_vals.append(phase_val)
                        x_coords.append(x_val)
                        z_values.append(z_val)
                        y_coords.append(y_val)

        if not x_coords:
            print("No valid data points found.")
            return

        plt.figure(figsize=(12, 10))
        
        # LogNorm makes the difference between 1, 2, 3 very visible 
        # while compressing the range between 10,000 and 1,000,000.
        norm = colors.LogNorm(vmin=min(z_values), vmax=max(z_values) * 20)

        marker_sizes = (np.log10(np.array(z_values)) * 70) + 200

        # all_marker_options = ['o', '^', 'x', '+', 's', 'D', 'v', '<', '>']
        marker_options = ['x', '+', '1']
        
        scatters = []

        for phase_idx, marker in enumerate(marker_options):
          mask = np.array(phase_vals) == 1 + phase_idx
          scatters.append(plt.scatter(
              np.array(x_coords)[mask],
              np.array(y_coords)[mask],
              c=np.array(z_values)[mask],
              cmap='gist_earth',
              norm=norm,
              marker=marker,
              linewidths=2.5,
              s=np.array(marker_sizes)[mask],
              alpha=1,
              rasterized=True,
              label=f'Phase {1 + phase_idx}'
          ))

        main_font_size = 25
        title_font_size = 30
        
        # Add a colorbar to explain what the colors mean
        cbar = plt.colorbar(scatters[0], pad=0.01)
        if not args.no_cbar_label:
            cbar.set_label('Number of forks', fontsize=main_font_size, labelpad=10)
        cbar.ax.yaxis.set_major_formatter(ticker.FuncFormatter(custom_formatter))
        cbar.ax.tick_params(axis='y', labelsize=main_font_size)

        plt.xlabel('Subdag work (ns)', fontsize=main_font_size)
        if not args.no_ylabel:
            plt.ylabel('Average work (ns) per fork in subdag', fontsize=main_font_size)
        # plt.title(plot_title, fontsize=title_font_size)
        plt.xscale('log')
        plt.yscale('log')

        ax = plt.gca()
        ax.set_ylim(bottom=10)
        ax.set_xlim(left=5000)

        # custom formatters for ticks
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(custom_formatter))
        ax.xaxis.set_major_formatter(ticker.FuncFormatter(custom_formatter))

        plt.xticks(fontsize=main_font_size)
        plt.yticks(fontsize=main_font_size)

        plt.legend(fontsize=main_font_size-4, loc='upper left', framealpha=0.9)

        plt.grid(True, which="both", linestyle='--', alpha=0.4, linewidth=1.1)
        
        output_file = f'scatter_plot_{filename}.pdf'
        plt.savefig(output_file, bbox_inches='tight', dpi=500)
        print(f"Success: Plot saved as '{output_file}'.")

    except FileNotFoundError:
        print(f"Error: The file '{filename}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    create_scatter_plot()