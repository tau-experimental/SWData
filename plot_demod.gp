set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'demodulator_phases.png'

set title "Выходы скользящих корреляторов: Фазы сигналов (10 Бод)"
set xlabel "Время (Сэмплы). Опорные точки: 800, 1600, 2400"
set ylabel "Фаза вектора (Градусы)"

set grid
set key top right

plot "demod_ideal_test.csv" using 1:2 with lines lw 2 title "Фаза 1300 Гц", \
     "demod_ideal_test.csv" using 1:3 with lines lw 2 title "Фаза 1400 Гц", \
     "demod_ideal_test.csv" using 1:4 with lines lw 2 title "Фаза 1500 Гц", \
     "demod_ideal_test.csv" using 1:5 with lines lw 2 title "Фаза 1600 Гц"

