set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'spectrum.png'

set title "Spectrum Snapshot"
set xlabel "Bin"
set ylabel "Magn"

set grid
set key top right

plot "fft_snapshot.csv" using 1:2 with lines lw 2 lt rgb "#FF901E" title "Magnitude"
