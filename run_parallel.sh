#!/bin/bash

echo "=== Compilando versión paralela (MPI + OpenMP) ==="
mpicc -fopenmp -o program_parallel program_parallel.c -lssl -lcrypto

if [ $? -eq 0 ]; then
    echo "Compilación exitosa"
    echo ""

    if [ $# -lt 2 ]; then
        echo "Uso:"
        echo "  Modo cifrado:"
        echo "    $0 input.txt output.bin"
        echo ""
        echo "  Modo brute force:"
        echo "    $0 encrypted.bin \"texto_buscar\""
        echo ""
        echo "Ejemplos:"
        echo "  $0 input.txt encrypted.bin"
        echo "  $0 encrypted.bin \"message with\""
        exit 1
    fi
    
    echo "=== Ejecutando con 6 procesos MPI ==="
    mpirun -np 6 ./program_parallel "$1" "$2"
else
    echo "Error en la compilación"
    exit 1
fi
