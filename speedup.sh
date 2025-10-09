#!/bin/bash

echo "=== Speedup vs Eficiencia: Versión Normal vs Paralela ==="
echo ""

# Configuración de archivos a probar
declare -A TEST_FILES
TEST_FILES["message2.bin"]=" ZEPHYRION-ECLIPSE-47A9F"
TEST_FILES["message_secret.bin"]="Alan"

# Número de procesos para ambas versiones
NUM_PROCS=4

# Número de ejecuciones
NUM_RUNS=10

# Archivo de resultados
RESULTS_FILE="resultados_comparacion.txt"

# Limpiar archivo anterior
> "$RESULTS_FILE"

# Función para ejecutar versión normal y extraer información
run_normal() {
    local file=$1
    local text=$2
    
    # Ejecutar y capturar salida completa
    output=$(mpirun -np "$NUM_PROCS" ./main "$file" "$text" 2>&1)
    
    # Extraer tiempo
    tiempo=$(echo "$output" | grep -iE "(time|tiempo|elapsed)" | grep -oP '\d+\.\d+' | head -1)
    
    if [ -z "$tiempo" ]; then
        tiempo=$(echo "$output" | grep -oP '\d+\.\d+' | head -1)
    fi
    
    # Extraer texto desencriptado
    decrypted=$(echo "$output" | grep -i "Decrypted text:" | sed 's/.*Decrypted text: //')
    
    # Extraer llave encontrada
    key=$(echo "$output" | grep -i "Key found:" | grep -oP '\d+' | head -1)
    
    echo "$tiempo|$decrypted|$key"
}

# Función para ejecutar versión paralela optimizada y extraer información
run_parallel() {
    local file=$1
    local text=$2
    
    # Ejecutar y capturar salida completa
    output=$(mpirun -np "$NUM_PROCS" ./main_parallel "$file" "$text" 2>&1)
    
    # Extraer tiempo
    tiempo=$(echo "$output" | grep -iE "(time|tiempo|elapsed)" | grep -oP '\d+\.\d+' | head -1)
    
    if [ -z "$tiempo" ]; then
        tiempo=$(echo "$output" | grep -oP '\d+\.\d+' | head -1)
    fi
    
    # Extraer texto desencriptado
    decrypted=$(echo "$output" | grep -i "Decrypted text:" | sed 's/.*Decrypted text: //')
    
    # Extraer llave encontrada
    key=$(echo "$output" | grep -i "Key found:" | grep -oP '\d+' | head -1)
    
    echo "$tiempo|$decrypted|$key"
}

# Función para calcular promedio
calculate_average() {
    local file=$1
    promedio=$(awk '{ sum += $1; n++ } END { if (n > 0) print sum / n; }' "$file")
    echo "$promedio"
}

# Función para calcular speedup y eficiencia
calculate_speedup() {
    local tiempo_normal=$1
    local tiempo_parallel=$2
    local num_procs=$3
    
    speedup=$(awk -v tn="$tiempo_normal" -v tp="$tiempo_parallel" 'BEGIN { printf "%.4f", tn / tp }')
    eficiencia=$(awk -v sp="$speedup" -v np="$num_procs" 'BEGIN { printf "%.4f", (sp / np) * 100 }')
    
    echo "$speedup $eficiencia"
}

echo "Compilando versión normal..."
mpicc -o main program.c -lssl -lcrypto

if [ $? -ne 0 ]; then
    echo "Error en la compilación de la versión normal"
    exit 1
fi

echo "Compilando versión paralela optimizada..."
mpicc -fopenmp -o main_parallel program_parallel.c -lssl -lcrypto

if [ $? -ne 0 ]; then
    echo "Error en la compilación de la versión paralela"
    exit 1
fi

echo "Compilación exitosa"
echo ""

# Iterar sobre cada archivo de prueba
for file in "${!TEST_FILES[@]}"; do
    text="${TEST_FILES[$file]}"
    
    echo "================================================" | tee -a "$RESULTS_FILE"
    echo "  Probando: $file" | tee -a "$RESULTS_FILE"
    echo "  Texto a buscar: $text" | tee -a "$RESULTS_FILE"
    echo "================================================" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    # Verificar que el archivo existe
    if [ ! -f "$file" ]; then
        echo "ERROR: Archivo $file no encontrado" | tee -a "$RESULTS_FILE"
        echo "" | tee -a "$RESULTS_FILE"
        continue
    fi
    
    # Variables para guardar información del desencriptado
    decrypted_text_normal=""
    key_found_normal=""
    decrypted_text_parallel=""
    key_found_parallel=""
    
    # ===== VERSIÓN NORMAL =====
    echo "--- Versión Normal (MPI, $NUM_PROCS procesos) ---" | tee -a "$RESULTS_FILE"
    echo "Realizando $NUM_RUNS mediciones..." | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    temp_file_normal="tiempos_normal.txt"
    > "$temp_file_normal"
    
    for i in $(seq 1 $NUM_RUNS); do
        echo "  Ejecución $i de $NUM_RUNS..."
        
        result=$(run_normal "$file" "$text")
        
        IFS='|' read -r tiempo decrypted key <<< "$result"
        
        if [ -n "$tiempo" ]; then
            echo "    Tiempo: $tiempo segundos"
            echo "$tiempo" >> "$temp_file_normal"
            
            # Guardar información del desencriptado (solo la primera vez)
            if [ $i -eq 1 ]; then
                decrypted_text_normal="$decrypted"
                key_found_normal="$key"
            fi
        else
            echo "    ERROR: No se pudo obtener el tiempo"
        fi
        
        sleep 1
    done
    
    echo "" | tee -a "$RESULTS_FILE"
    echo "Tiempos individuales:" | tee -a "$RESULTS_FILE"
    cat "$temp_file_normal" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    promedio_normal=$(calculate_average "$temp_file_normal")
    echo "Tiempo promedio (Normal): $promedio_normal segundos" | tee -a "$RESULTS_FILE"
    
    # Mostrar información del desencriptado
    if [ -n "$key_found_normal" ]; then
        echo "Llave encontrada: $key_found_normal" | tee -a "$RESULTS_FILE"
    fi
    if [ -n "$decrypted_text_normal" ]; then
        echo "Texto desencriptado: $decrypted_text_normal" | tee -a "$RESULTS_FILE"
    fi
    echo "" | tee -a "$RESULTS_FILE"
    
    rm "$temp_file_normal"
    
    # ===== VERSIÓN PARALELA OPTIMIZADA =====
    echo "--- Versión Paralela Optimizada (MPI + OpenMP, $NUM_PROCS procesos) ---" | tee -a "$RESULTS_FILE"
    echo "Realizando $NUM_RUNS mediciones..." | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    temp_file_parallel="tiempos_parallel.txt"
    > "$temp_file_parallel"
    
    for i in $(seq 1 $NUM_RUNS); do
        echo "  Ejecución $i de $NUM_RUNS..."
        
        result=$(run_parallel "$file" "$text")
        
        IFS='|' read -r tiempo decrypted key <<< "$result"
        
        if [ -n "$tiempo" ]; then
            echo "    Tiempo: $tiempo segundos"
            echo "$tiempo" >> "$temp_file_parallel"
            
            # Guardar información del desencriptado (solo la primera vez)
            if [ $i -eq 1 ]; then
                decrypted_text_parallel="$decrypted"
                key_found_parallel="$key"
            fi
        else
            echo "    ERROR: No se pudo obtener el tiempo"
        fi
        
        sleep 1
    done
    
    echo "" | tee -a "$RESULTS_FILE"
    echo "Tiempos individuales:" | tee -a "$RESULTS_FILE"
    cat "$temp_file_parallel" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    promedio_parallel=$(calculate_average "$temp_file_parallel")
    echo "Tiempo promedio (Paralelo Optimizado): $promedio_parallel segundos" | tee -a "$RESULTS_FILE"
    
    # Mostrar información del desencriptado
    if [ -n "$key_found_parallel" ]; then
        echo "Llave encontrada: $key_found_parallel" | tee -a "$RESULTS_FILE"
    fi
    if [ -n "$decrypted_text_parallel" ]; then
        echo "Texto desencriptado: $decrypted_text_parallel" | tee -a "$RESULTS_FILE"
    fi
    echo "" | tee -a "$RESULTS_FILE"
    
    rm "$temp_file_parallel"
    
    # ===== ANÁLISIS DE SPEEDUP Y EFICIENCIA =====
    echo "=== Resultados ===" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    read speedup eficiencia <<< $(calculate_speedup "$promedio_normal" "$promedio_parallel" "$NUM_PROCS")
    
    echo "Archivo: $file" | tee -a "$RESULTS_FILE"
    echo "Texto buscado: $text" | tee -a "$RESULTS_FILE"
    if [ -n "$decrypted_text_normal" ]; then
        echo "Texto desencriptado: $decrypted_text_normal" | tee -a "$RESULTS_FILE"
        echo "Llave encontrada: $key_found_normal" | tee -a "$RESULTS_FILE"
    fi
    echo "" | tee -a "$RESULTS_FILE"
    echo "Tiempo promedio Normal:              $promedio_normal segundos" | tee -a "$RESULTS_FILE"
    echo "Tiempo promedio Paralelo Optimizado: $promedio_parallel segundos" | tee -a "$RESULTS_FILE"
    echo "Speedup:                             $speedup" | tee -a "$RESULTS_FILE"
    echo "Eficiencia:                          $eficiencia%" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
done

echo "================================================" | tee -a "$RESULTS_FILE"
echo "  Benchmark completado" | tee -a "$RESULTS_FILE"
echo "================================================" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"
echo "Resultados guardados en: $RESULTS_FILE" | tee -a "$RESULTS_FILE"