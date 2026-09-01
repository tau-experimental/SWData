set datafile separator ','
stats "mls31.csv" using 1 nooutput
set xrange [STATS_min:STATS_max]
set terminal pngcairo size 1200, 800 enhanced font 'Verdana,10'
set output 'mls31.png'

set multiplot layout 2,1 columnsfirst

# ОБЩИЕ НАСТРОЙКИ ДЛЯ ОБЕИХ ПАНЕЛЕЙ
set lmargin screen 0.10
set rmargin screen 0.95
set grid x y



# --- ВЕРХНЯЯ ПАНЕЛЬ (Величина 1) ---
set tmargin screen 0.93  
set bmargin screen 0.50  

unset xtics        
unset xlabel
set title "Наладка S/C sync" offset 0,-0.5 
set ylabel "SC power"
set yrange [-1:2]
set ytics

set key top right

plot "mls31.csv" using 1:2 with lines lw 2 lt rgb "#FF007F" title "Is synchronized?"

# --- НИЖНЯЯ ПАНЕЛЬ (Величина 2) ---
set tmargin screen 0.50  
set bmargin screen 0.10  

set xtics                
set xlabel "Время (Сэмплы)"
unset title

set ylabel "Needle"
set yrange [-200:200]
set ytics

plot "mls31.csv" using 1:3 with lines lw 2 lt rgb "#FF7F00" title "MLS Needle" , \
    "mls31.csv" using 1:4 with lines lw 2 lt rgb "#7FFF00" title "DerotPhase" 

unset multiplot

