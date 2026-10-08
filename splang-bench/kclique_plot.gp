# Plots a kclique result CSV (written by kclique_run.sh): memory usage vs. thread count.
# Usage: gnuplot -c kclique_plot.gp <name> <title>
#   reads <name>.csv and writes <name>.png
# Header: size,threads,Observed HWM,S,Predicted,R_inf  (size = k, Predicted = S + P*R1*_partial)
#
# Same content as the nqueens/ann plots. R_inf is orders of magnitude above the rest, so the
# broken y-axis is drawn as two stacked panels sharing the thread axis: a thin one around
# R_inf above the main one.
set datafile separator ','
set datafile columnheaders
set terminal pngcairo size 1600,1200 enhanced font ',22' linewidth 2 pointscale 2
set output ARG1.'.png'
set decimalsign locale 'en_US.UTF-8'
set format y "%'.0f"

data = ARG1.'.csv'

# Thousands separators for the key, which the locale's %' does not reach.
commas(x) = x < 1000 ? sprintf('%d', x) : commas(x / 1000).sprintf(',%03d', x % 1000)

stats data using 'Predicted' nooutput name 'PRED'
stats data using 'Observed HWM' nooutput name 'HWM'
stats data using 'R_inf' nooutput name 'RINF'

# Main panel: a round step of about 1/6 of the largest value, and the axis just above it.
nice(x) = (m = 10.0**floor(log10(x)), x / m <= 2 ? 2 * m : x / m <= 5 ? 5 * m : 10 * m)
top = PRED_max > HWM_max ? PRED_max : HWM_max
step = nice(top / 6)
B1 = ceil(top / step) * step

# Upper panel: the two round ticks that bracket R_inf.
hstep = nice(RINF_max / 50)
lo = floor(RINF_max / hstep) * hstep
hi = lo + hstep

L = 0.17; R = 0.96          # panel edges, screen coordinates
UT = 0.90; UB = 0.80        # upper panel
MT = 0.76; MB = 0.11        # main panel

set multiplot
set xrange [0:130]
set grid

# Upper panel: R_inf only; no bottom border, x tics without labels.
set lmargin at screen L; set rmargin at screen R
set tmargin at screen UT; set bmargin at screen UB
set border 2 + 4 + 8
set title ARG2
set yrange [lo:hi]
set ytics (lo, hi)
set xtics format ''
set xtics (1, 4, 8, 16, 32, 64, 128)
unset key
plot data using 'threads':'R_inf' with lines lw 2 lc 3 notitle

# Main panel: no top border; the key lists R_inf too, although its line is off-scale here.
set tmargin at screen MT; set bmargin at screen MB
set border 1 + 2 + 8
unset title
set yrange [0:B1]
set ytics 0, step, B1
set xtics format '%g'
set xlabel 'Threads'
set ylabel 'Memory usage (words)' offset 0, (UT - MT) / 2 * 30
set key top left Left reverse box opaque width 2

# Break marks on both edges of the gap.
do for [x in sprintf('%f %f', L, R)] {
    do for [y in sprintf('%f %f', UB, MT)] {
        set arrow from screen x - 0.008, screen y - 0.012 to screen x + 0.008, screen y + 0.012 nohead lw 2 front
    }
}

plot data using 'threads':'Observed HWM' with linespoints lw 2 pt 7 lc 1 title 'Observed HWM', \
     data using 'threads':'Predicted' with linespoints lw 2 pt 5 lc 2 title 'S + R_1^*', \
     data using 'threads':'R_inf' with lines lw 2 lc 3 title 'R_{/Symbol \245} = '.commas(int(RINF_max))
unset multiplot
