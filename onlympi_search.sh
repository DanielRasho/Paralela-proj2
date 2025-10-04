#!/bin/bash

echo "=== Benchmark de DES Brute Force ==="
echo "Realizando 10 mediciones..."
echo ""

# Archivo para guardar los tiempos
> tiempos.txt

# Se realizan 10 ejecuciones
for i in {1..10}
do
    echo "Ejecución $i de 10..."
    
    # Ejecuta y extrae el tiempo
    output=$(mpirun -np 4 ./main input.txt 2>&1)
    
    # Extraer solo el número del tiempo
    tiempo=$(echo "$output" | grep "Time elapsed" | grep -oP '\d+\.\d+')
    
    echo "  Tiempo: $tiempo segundos"
    echo "$tiempo" >> tiempos.txt
    
    sleep 1
done

echo ""
echo "=== Resultados ==="
echo "Tiempos individuales:"
cat tiempos.txt

# Calcula el tiempo promedio
promedio=$(awk '{ sum += $1; n++ } END { if (n > 0) print sum / n; }' tiempos.txt)
echo ""
echo "Tiempo promedio: $promedio segundos"