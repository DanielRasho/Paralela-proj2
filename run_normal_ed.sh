#!/bin/bash

echo "=== Compilando versión normal (solo MPI) para encriptar y desencriptar ==="
mpicc -o main program.c -lssl -lcrypto

if [ $? -eq 0 ]; then
    echo "¡Compilación exitosa!"
    echo ""
    echo "=== Ejecutando con 4 procesos MPI para encriptar y desencriptar ==="
    mpirun -np 4 ./main input.txt
else
    echo "Error en la compilación"
    exit 1
fi
