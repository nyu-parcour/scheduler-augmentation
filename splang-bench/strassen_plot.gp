# Plots strassen_result.csv (written by runner.sh): memory usage vs. thread count.
# Header: size,threads,Observed HWM,S,Predicted,R_inf  (Predicted = S + P*R1*_partial)
set datafile separator ','
set datafile columnheaders
set terminal pngcairo size 1600,1200 enhanced font ',22' linewidth 2 pointscale 2
set output 'strassen_result.png'

data = 'strassen_result.csv'

stats data using 'size' nooutput name 'SIZE'
stats data using 'R_inf' nooutput name 'RINF'

# Break the y-axis between B1 and B2: values in the gap map to NaN, values above are shifted
# down so that B2 sits GAP plot units above B1, leaving an empty band between the two halves.
# Above the break, show the two regular ticks that bracket R_inf.
step = 1000000000
B1 = 9000000000
lo = floor(RINF_max / step) * step
hi = lo + step
B2 = lo - step / 4
GAP = step / 2
f(y) = y <= B1 ? y : y < B2 ? NaN : y - (B2 - B1) + GAP
g(y) = y <= B1 ? y : y < B1 + GAP ? NaN : y + (B2 - B1) - GAP
set nonlinear y via f(y) inverse g(y)
set yrange [0:hi]
set ytics 0, step, B1
set ytics add (lo, hi)
set format y '%.0f'

# Blank out the band (grid and vertical borders) and put break marks on both of its edges.
set border back
set object 1 rect from graph -0.01, first B1 to graph 1.01, first B2 fc rgb 'white' fs solid noborder noclip front
do for [x in '0 1'] {
    do for [y in sprintf('%d %d', B1, B2)] {
        do for [s in '-1 1'] {
            set arrow from graph x, first y rto screen s * 0.008, screen s * 0.012 nohead lw 2 front
        }
    }
}

set title sprintf('Strassen (N = %d): memory usage vs. threads', SIZE_min)
set xlabel 'Threads'
set ylabel 'Memory usage (words)'
set xrange [0:130]
set key at graph 0.02, first B1 * 0.97 top left Left reverse box opaque width 2
set grid

# 1 and 2 are too close together on a linear axis to label both.
plot data using 'threads':'Observed HWM':xtic(column('threads') == 2 ? '' : stringcolumn('threads')) with linespoints lw 2 pt 7 title 'Observed HWM', \
     data using 'threads':'Predicted' with linespoints lw 2 pt 5 title 'S + R_1^*', \
     data using 'threads':'R_inf' with lines lw 2 title sprintf('R_{/Symbol \245} = %d', RINF_max)
