set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'barker.png'

set title "Наладка детектора Баркер"
set xlabel "Время (Сэмплы)"
set ylabel "Correlation"

set grid
set key top right

plot "barker.csv" using 1:2 with lines lw 2 lt rgb "#FF901E" title "Correlation"
#        "barker.csv" using 1:3 with lines lw 1.5 lt rgb "#00FF7F" title "Noise Floor",
