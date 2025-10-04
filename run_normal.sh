#!/bin/bash

echo "=== Compilando versión normal (solo MPI) ==="
mpicc -o program program.c -lssl -lcrypto

if [ $? -eq 0 ]; then
    echo "Compilación exitosa"
    echo ""
    echo "=== Ejecutando con 4 procesos MPI ==="
    mpirun -np 4 ./program input.txt
else
    echo "Error en la compilación"
    exit 1
fi
