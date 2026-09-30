# Plots nqueens_result.csv (written by runner.sh): memory usage vs. thread count.
# Header: size,threads,Observed HWM,S,Predicted,R_inf  (Predicted = S + P*R1*_partial)
set datafile separator ','
set datafile columnheaders
set terminal pngcairo size 1600,1200 enhanced font ',22' linewidth 2 pointscale 2
set output 'strassen_result.png'

data = 'strassen_result.csv'

stats data using 'size' nooutput name 'SIZE'
stats data using 'threads' nooutput name 'THREADS'
stats data using 'Observed HWM' nooutput name 'HWM'
stats data using 'R_inf' nooutput name 'RINF'

# Log y-axis spanning whole decades up to R_inf.
set logscale y 10
set yrange [1000000:10**ceil(log10(RINF_max))]
set format y '%.0f'

set title sprintf('Strassen (N = %d): memory usage vs. threads', SIZE_min)
set xlabel 'Threads'
set ylabel 'Memory usage (words)'
set xrange [0:130]
set key bottom right Left reverse box opaque width 2
set grid

# 1 and 2 are too close together on a linear axis to label both.

plot data using 'threads':'Observed HWM':xtic(column('threads') == 2 ? '' : stringcolumn('threads')) with linespoints lw 2 pt 7 title 'Observed HWM', \
     data using 'threads':'Predicted' with linespoints lw 2 pt 5 title 'S + R_1^*', \
     data using 'threads':'R_inf' with lines lw 2 title sprintf('R_{/Symbol \245} = %d', RINF_max)
