set terminal pngcairo size 2000,1000 font "Arial,10"
set output "DDS_Dump.png"
set datafile separator ","
set grid ytics

set title "Модулятор и коррелятор"
set xlabel "Индекс"
set ylabel "Магнитуда"
set y2tics

# Горизонтальные линии идеальных границ удержания (номинал +-3 сэмпла)
#set arrow from 0,3 to 120,3 nohead lc rgb "red" dt 2
#set arrow from 0,-3 to 120,-3 nohead lc rgb "red" dt 2

plot "DDS_Dump.csv" using 1:2 axes x1y1 title "IF.re" with lines lw 1 lc rgb '#7f77e4' ,\
    "DDS_Dump.csv" using 1:3 axes x1y1 title "IF.im" with lines lw 1 lc rgb '#7fe477' ,\
    "DDS_Dump.csv" using 1:4 axes x1y2 title "CorrFactor" with lines lw 2 lc rgb '#771f14'

