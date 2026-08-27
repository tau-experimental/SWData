set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'demodulator_sync_aru.png'

set title "Тактовая синхронизация с адаптивным порогом (АРУ) под шумом"
set xlabel "Время (Сэмплы)"
set ylabel "Амплитуда"

set grid
set key top right

# column(2) - SumMag, column(3) - Strobe, column(5) - NoiseFloor
plot "sync_test.csv" using 1:2 with lines lw 2 lt rgb "#1E90FF" title "Сумма модулей (Холмы)", \
     "sync_test.csv" using 1:5 with lines lw 1.5 lt rgb "#00FF7F" title "Текущий фон шума", \
    "sync_test.csv" using 1:(column(5)*1.4) with lines lw 2 lt rgb "#FFAA00" title "Динамический порог (x1.4)", \
     "sync_test.csv" using 1:3 with impulses lw 3 lt rgb "#FF0000" title "Строб решения"

