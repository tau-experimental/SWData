set terminal pngcairo size 1000,500 font "Arial,10"
set output "gardner_jitter_verification.png"
set datafile separator ","
set grid

set title "Фазовое отклонение (Джиттер) строба Гарднера под шумом" font "Arial,12"
set xlabel "Индекс извлеченного символа"
set ylabel "Отклонение от истинного центра (в сэмплах АЦП)"

# Горизонтальные линии идеальных границ удержания (номинал +-3 сэмпла)
set arrow from 0,3 to 120,3 nohead lc rgb "red" dt 2
set arrow from 0,-3 to 120,-3 nohead lc rgb "red" dt 2

plot "3_gardner_jitter.csv" using 1:2 title "Ошибка слежения тайминга" with linespoints lw 2 pt 7 ps 0.8 lc rgb '#1f77b4'

