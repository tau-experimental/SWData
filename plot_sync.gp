set terminal pngcairo size 800,500 enhanced font "Arial,10"
set output 'sync_profile.png'
set title "MFSK Modem Sync Correlation Profile"
set xlabel "Symbol Index"
set ylabel "Correlation Metric"
set grid
set datafile separator ","
set key autotitle columnhead
plot 'sync_debug.csv' using 1:2 with linespoints lw 2 lc rgb '#007ACC' title 'Correlation Value', \
     70.0 with lines lt 2 lc rgb 'red' title 'Sync Threshold'
