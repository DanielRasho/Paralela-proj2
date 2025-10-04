#!/bin/bash

echo "=== Compilando versión paralela (MPI + OpenMP) ==="
mpicc -fopenmp -o program_parallel program_parallel.c -lssl -lcrypto

if [ $? -eq 0 ]; then
    echo "Compilación exitosa"
    echo ""
    echo "=== Ejecutando con 4 procesos MPI ==="
    mpirun -np 4 ./program_parallel input.txt
else
    echo "Error en la compilación"
    exit 1
fi
