# Настройка вывода в PNG с хорошим разрешением
set terminal pngcairo size 1200,600 font "Arial,10"
set output "timing_verification.png"

# Настройка разделителя и сетки
set datafile separator ","
set grid xtics ytics ls 12 lc rgb '#dddddd' lt 1

set title "Проверка тактовой синхронизации Гарднера (31.25 Бод)" font "Arial,12"
set xlabel "Отсчеты АЦП (8 кГц)"
set ylabel "Амплитуда сигнала (int16)"

# Диапазон отображения (покажем небольшой кусочек кадра для детального рассмотрения ступенек)
set xrange [9000:23500]
set yrange [-30000:30000]

# Оформление стилей линий
set style line 1 lc rgb '#0072bd' lt 1 lw 1.5  # Сигнал I
set style line 2 lc rgb '#d95319' lt 1 lw 2    # Строб Центра
set style line 3 lc rgb '#2ca02c' lt 1 lw 2    # Строб Стыка

# Отрисовка
plot "3_timing_diagnostic.csv" using 1:2 title "Демодулированный сигнал (I)" with lines ls 1, \
     "3_timing_diagnostic.csv" using 1:3 title "Строб центра символа (Гарднер)" with impulses ls 2, \
     "3_timing_diagnostic.csv" using 1:4 title "Строб границы символа (Стык)" with impulses ls 3

