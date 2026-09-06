set title "Сигнал и Шум в БПФ-256"
set xlabel "Частота (Гц)"
set ylabel "Мощность (у.е.)"
set grid
set xrange [800:1200] # Зуммируем область вокруг 1000 Гц
plot "spectrum.csv" using 2:3 with lines title "Мгновенный спектр", \
     "spectrum.csv" using 2:4 with lines lw 2 title "Накопленный спектр"
pause -1 "Нажмите Enter для выхода"

