#!/bin/bash

echo "========================================================="
echo "  Comparación con ±2 Procesos"
echo "========================================================="
echo ""

TEST_FILE="message_secret.bin"
SEARCH_TEXT="Alan"

# Configuración de número de procesos
BASE_PROCS=4
PROC_MINUS_2=$((BASE_PROCS - 2))  # 2 procesos
PROC_PLUS_2=$((BASE_PROCS + 2))   # 6 procesos

# Número de ejecuciones por configuración
NUM_RUNS=10

# Archivos de resultados
RESULTS_FILE="comparacion_procesos.txt"
SUMMARY_FILE="resumen_comparacion.txt"

# Limpiar archivos anteriores
> "$RESULTS_FILE"
> "$SUMMARY_FILE"

# Verificar que el archivo existe
if [ ! -f "$TEST_FILE" ]; then
    echo "ERROR: Archivo $TEST_FILE no encontrado"
    exit 1
fi

# Función para ejecutar y extraer información
run_test() {
    local num_procs=$1
    local version=$2  # "normal" o "parallel"
    
    if [ "$version" == "normal" ]; then
        output=$(mpirun -np "$num_procs" ./main "$TEST_FILE" "$SEARCH_TEXT" 2>&1)
    else
        output=$(mpirun -np "$num_procs" ./main_parallel "$TEST_FILE" "$SEARCH_TEXT" 2>&1)
    fi
    
    tiempo=$(echo "$output" | grep -iE "(time|tiempo|elapsed)" | grep -oP '\d+\.\d+' | head -1)
    
    if [ -z "$tiempo" ]; then
        tiempo=$(echo "$output" | grep -oP '\d+\.\d+' | head -1)
    fi
    
    decrypted=$(echo "$output" | grep -i "Decrypted text:" | sed 's/.*Decrypted text: //')
    key=$(echo "$output" | grep -i "Key found:" | grep -oP '\d+' | head -1)
    
    echo "$tiempo|$decrypted|$key"
}

# Calcular promedio
calculate_average() {
    local file=$1
    awk '{ sum += $1; n++ } END { if (n > 0) print sum / n; }' "$file"
}

# Función para calcular speedup y eficiencia
calculate_metrics() {
    local tiempo_normal=$1
    local tiempo_parallel=$2
    local num_procs=$3
    
    speedup=$(awk -v tn="$tiempo_normal" -v tp="$tiempo_parallel" 'BEGIN { printf "%.4f", tn / tp }')
    eficiencia=$(awk -v sp="$speedup" -v np="$num_procs" 'BEGIN { printf "%.4f", (sp / np) * 100 }')
    
    echo "$speedup $eficiencia"
}

# Compilar programas
echo "Compilando programas..."
echo ""

mpicc -o main program.c -lssl -lcrypto
if [ $? -ne 0 ]; then
    echo "ERROR: Compilación de versión normal falló"
    exit 1
fi

mpicc -fopenmp -o main_parallel program_parallel.c -lssl -lcrypto
if [ $? -ne 0 ]; then
    echo "ERROR: Compilación de versión paralela falló"
    exit 1
fi

echo "Compilación exitosa"
echo ""

# Encabezado de archivos
{
    echo "========================================================="
    echo "  BENCHMARK: COMPARACIÓN ±2 PROCESOS"
    echo "========================================================="
    echo ""
    echo "Archivo:        $TEST_FILE"
    echo "Búsqueda:       $SEARCH_TEXT"
    echo "Ejecuciones:    $NUM_RUNS por configuración"
    echo "Configuraciones:"
    echo "  - BASE - 2:   $PROC_MINUS_2 procesos"
    echo "  - BASE:       $BASE_PROCS procesos"
    echo "  - BASE + 2:   $PROC_PLUS_2 procesos"
    echo ""
    echo "========================================================="
    echo ""
} | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"

declare -A TIEMPOS_NORMAL
declare -A TIEMPOS_PARALLEL
declare -A SPEEDUPS
declare -A EFICIENCIAS

# Lista de configuraciones
CONFIGS=("$PROC_MINUS_2" "$BASE_PROCS" "$PROC_PLUS_2")
CONFIG_NAMES=("BASE-2" "BASE" "BASE+2")

# Variable para guardar texto desencriptado 
DECRYPTED_TEXT=""
KEY_FOUND=""

# Iterar sobre cada configuración
for idx in "${!CONFIGS[@]}"; do
    num_procs="${CONFIGS[$idx]}"
    config_name="${CONFIG_NAMES[$idx]}"
    
    echo "=========================================================" | tee -a "$RESULTS_FILE"
    echo "  CONFIGURACIÓN: $config_name ($num_procs procesos)" | tee -a "$RESULTS_FILE"
    echo "=========================================================" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    # ===== VERSIÓN NORMAL =====
    echo "--- Versión Normal (MPI) ---" | tee -a "$RESULTS_FILE"
    echo "Ejecutando $NUM_RUNS pruebas con $num_procs procesos..." | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    temp_normal="temp_normal_${num_procs}.txt"
    > "$temp_normal"
    
    for i in $(seq 1 $NUM_RUNS); do
        printf "  [%2d/%2d] Normal, %d procs... " $i $NUM_RUNS $num_procs
        
        result=$(run_test "$num_procs" "normal")
        IFS='|' read -r tiempo decrypted key <<< "$result"
        
        if [ -n "$tiempo" ]; then
            echo "$tiempo s"
            echo "$tiempo" >> "$temp_normal"
            
            if [ -z "$DECRYPTED_TEXT" ] && [ -n "$decrypted" ]; then
                DECRYPTED_TEXT="$decrypted"
                KEY_FOUND="$key"
            fi
        else
            echo "ERROR"
        fi
        
        sleep 0.5
    done
    
    echo "" | tee -a "$RESULTS_FILE"
    
    promedio_normal=$(calculate_average "$temp_normal")
    
    echo "Tiempos: $(cat $temp_normal | tr '\n' ' ')" | tee -a "$RESULTS_FILE"
    echo "Promedio:    $promedio_normal segundos" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    TIEMPOS_NORMAL[$num_procs]="$promedio_normal"
    
    rm "$temp_normal"
    
    # ===== VERSIÓN PARALELA =====
    echo "--- Versión Paralela (MPI + OpenMP) ---" | tee -a "$RESULTS_FILE"
    echo "Ejecutando $NUM_RUNS pruebas con $num_procs procesos..." | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    temp_parallel="temp_parallel_${num_procs}.txt"
    > "$temp_parallel"
    
    for i in $(seq 1 $NUM_RUNS); do
        printf "  [%2d/%2d] Paralelo, %d procs... " $i $NUM_RUNS $num_procs
        
        result=$(run_test "$num_procs" "parallel")
        IFS='|' read -r tiempo decrypted key <<< "$result"
        
        if [ -n "$tiempo" ]; then
            echo "$tiempo s"
            echo "$tiempo" >> "$temp_parallel"
        else
            echo "ERROR"
        fi
        
        sleep 0.5
    done
    
    echo "" | tee -a "$RESULTS_FILE"
    
    promedio_parallel=$(calculate_average "$temp_parallel")
    echo "Tiempos: $(cat $temp_parallel | tr '\n' ' ')" | tee -a "$RESULTS_FILE"
    echo "Promedio:    $promedio_parallel segundos" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
    TIEMPOS_PARALLEL[$num_procs]="$promedio_parallel"
    
    rm "$temp_parallel"
    
    read speedup eficiencia <<< $(calculate_metrics "$promedio_normal" "$promedio_parallel" "$num_procs")
    
    SPEEDUPS[$num_procs]="$speedup"
    EFICIENCIAS[$num_procs]="$eficiencia"
    
    echo "=== Métricas de Rendimiento ===" | tee -a "$RESULTS_FILE"
    echo "Speedup:       $speedup" | tee -a "$RESULTS_FILE"
    echo "Eficiencia:    $eficiencia %" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    
done

echo "=========================================================" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
echo "  RESUMEN COMPARATIVO" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
echo "=========================================================" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
echo "" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"

if [ -n "$DECRYPTED_TEXT" ]; then
    echo "Texto desencriptado: $DECRYPTED_TEXT" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
    echo "Llave encontrada: $KEY_FOUND" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
    echo "" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
fi

{
    printf "%-15s | %-20s | %-20s | %-12s | %-12s\n" \
           "Configuración" "Tiempo Normal (s)" "Tiempo Paralelo (s)" "Speedup" "Eficiencia (%)"
    echo "--------------------------------------------------------------------------------------------"
} | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"

for idx in "${!CONFIGS[@]}"; do
    num_procs="${CONFIGS[$idx]}"
    config_name="${CONFIG_NAMES[$idx]}"
    
    t_normal="${TIEMPOS_NORMAL[$num_procs]}"
    t_parallel="${TIEMPOS_PARALLEL[$num_procs]}"
    speedup="${SPEEDUPS[$num_procs]}"
    eficiencia="${EFICIENCIAS[$num_procs]}"
    
    printf "%-15s | %-20s | %-20s | %-12s | %-12s\n" \
           "$config_name ($num_procs)" "$t_normal" "$t_parallel" "$speedup" "$eficiencia" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
done

echo "" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"

echo "¡Benchmark completado exitosamente!" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
echo "" | tee -a "$RESULTS_FILE" "$SUMMARY_FILE"
echo "Resultados detallados: $RESULTS_FILE" | tee -a "$SUMMARY_FILE"
echo "Resumen ejecutivo:     $SUMMARY_FILE" | tee -a "$SUMMARY_FILE"