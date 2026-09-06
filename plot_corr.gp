set terminal pngcairo size 1000,500 font "Arial,10"
set output "preamble_correlation.png"
set datafile separator ","
set grid

set title "Выход скользящего коррелятора преамбулы (SNR = +3 дБ)" font "Arial,12"
set xlabel "Индекс извлеченного символа (31.25 Бод)"
set ylabel "Амплитуда корреляционного пика"

plot "3_preamble_diagnostic.csv" using 1:2 title "Функция взаимной корреляции" with lines lw 2 lc rgb '#ff7f0e'

