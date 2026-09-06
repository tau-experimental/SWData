set terminal pngcairo size 1024,768 enhanced font "Verdana,10"
set output "spectrogram_low_if.png"

set title "Спектрограмма КВ-канала в полосе Low-IF (±125 Гц от несущей)"
set xlabel "Время (секунды)"
set ylabel "Отклонение от ожидаемой несущей 1000 Гц (Гц)"

set pm3d map interpolate 3,3
set palette rgbformulae 21,22,23 # Контрастная палитра для выделения слабых сигналов

set yrange [-125:125] # Честные плюс-минус 125 Гц обзора
set grid

splot "waterfall.csv" using 1:2:3 with pm3d notitle

