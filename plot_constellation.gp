set datafile separator ','
set terminal pngcairo size 800, 800 enhanced font 'Verdana,10'
set output 'signal_constellation_raw.png'

set title "Сырое сигнальное созвездие КВ-модема (100 символов под шумом)"
set xlabel "In-phase (Re)"
set ylabel "Quadrature (Im)"

set size square
set grid
set xzeroaxis lt rgb "#888888"
set yzeroaxis lt rgb "#888888"

# Позволим gnuplot самому выбрать оптимальный масштаб под размах энергии
set autoscale x
set autoscale y

plot "constellation.csv" using 1:2 with points pt 7 ps 2.5 lc rgb "#FF0033" title "Тон 1300 Гц", \
     "" using 3:4 with points pt 7 ps 2.5 lc rgb "#EEEE00" title "Тон 1400 Гц", \
     "" using 5:6 with points pt 7 ps 2.5 lc rgb "#00FF7F" title "Тон 1500 Гц", \
     "" using 7:8 with points pt 7 ps 2.5 lc rgb "#1E90FF" title "Тон 1600 Гц"

