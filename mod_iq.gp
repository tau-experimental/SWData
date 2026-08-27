set datafile separator ','
set terminal pngcairo size 1200, 600 enhanced font 'Verdana,10'
set output 'modulator_raw_iq.png'

set title "Временная диаграмма I/Q на границах символов"
set xlabel "Время (Сэмплы)"
set ylabel "Амплитуда"

set grid
set key top right

# Ограничим просмотр зоной вокруг первой границы символа (сэмплы 100-220), 
# чтобы рассмотреть саму структуру синусоиды
set xrange [100:220]

plot "pure_modulator_test.csv" using 1:2 with lines lw 2 lt rgb "#1E90FF" title "Сигнал I (Cos)", \
     "" using 1:3 with lines lw 2 lt rgb "#00FF7F" title "Сигнал Q (Sin)", \
     "" using 1:4 with impulses lw 3 lt rgb "#FF0000" title "Граница символа (сэмпл 160)"

