set title "Отслеживание ионосферного дрейфа"
set xlabel "Отсчеты"
set ylabel "Сдвиг частоты (Гц)"
set grid
plot "tracker.csv" using 1:2 with lines title "Реальный дрейф канала", \
     "tracker.csv" using 1:3 with points pt 7 ps 0.5 title "Оценка приемника"
pause -1 "Нажмите Enter для выхода"

