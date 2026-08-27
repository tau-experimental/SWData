set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'dsp_modulator_test.png'

set title "Тест модулятора"
set xlabel "Время (Сэмплы)."
set ylabel "Модуль амплитуды выходов со смещением"

set grid
set key top right

bias = 1.0

plot "modulator_test.csv" using 1:4 with lines lw 2 lt rgb "#1E90FF" title "Выход 1300 Гц", \
     "" using 1:(column(5) + bias) with lines lw 2 lt rgb "#FF8C00" title "Выход 1400 Гц", \
     "" using 1:(column(6) + bias*2) with lines lw 2 lt rgb "#FF1493" title "Выход 1500 Гц", \
     "" using 1:(column(7) + bias*3) with lines lw 2 lt rgb "#00CED1" title "Выход 1600 Гц"

