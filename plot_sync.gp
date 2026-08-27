set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'demodulator_auto_sync.png'

set title "Автоматическая тактовая синхронизация КВ-приемника (10 Бод)"
set xlabel "Время (Сэмплы). Рандомный старт на 650 сэмпле"
set ylabel "Амплитуда суммы модулей / Строб"

set grid
set key top right

plot "sync_test.csv" using 1:2 with lines lw 2 lt rgb "#1E90FF" title "Сумма энергий каналов (Холмы)", \
     "" using 1:3 with impulses lw 3 lt rgb "#FF0000" title "Авто-Выставленный Строб (Решение)"

