set datafile separator ','
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'demodulator_diff_steps.png'

set title "Дифференциальные фазовые ступеньки КВ-модема (10 Бод)"
set xlabel "Время (Сэмплы)"
set ylabel "Разность фаз (Градусы)"

set grid
set key top right
set yrange [-180:180]

plot "demod_diff_test.csv" using 1:2 with lines lw 2 title "Дифф. Фаза 1300 Гц", \
     "" using 1:3 with lines lw 2 title "Дифф. Фаза 1400 Гц", \
     "" using 1:4 with lines lw 2 title "Дифф. Фаза 1500 Гц", \
     "" using 1:5 with lines lw 2 title "Дифф. Фаза 1600 Гц", \
     "" using 1:6 with impulses lw 3 lt rgb "#FF0000" title "Точки стробирования (Решение)"

