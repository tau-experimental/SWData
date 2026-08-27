set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'dsp_stress_test.png'

set title "Результаты стресс-теста комплексных фильтров (100 Гц полоса)"
set xlabel "Время (Сэмплы). Зоны: 0-300 Идеал | 300-600 Дрейф +25Гц | 600-900 Плавает частота | 900-1200 Шум"
set ylabel "Модуль амплитуды выходов со смещением"

set grid
set key top right

bias = 1.0

plot "stress_test.csv" using 1:4 with lines lw 2 lt rgb "#1E90FF" title "Выход 1300 Гц", \
     "" using 1:(column(5) + bias) with lines lw 2 lt rgb "#FF8C00" title "Выход 1400 Гц", \
     "" using 1:(column(6) + bias*2) with lines lw 2 lt rgb "#FF1493" title "Выход 1500 Гц", \
     "" using 1:(column(7) + bias*3) with lines lw 2 lt rgb "#00CED1" title "Выход 1600 Гц"

