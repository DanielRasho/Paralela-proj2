#!/bin/bash

echo "=== Compilando versión normal (solo MPI) para desencriptar mensaje encriptado en .bin ==="
mpicc -o main program.c -lssl -lcrypto

if [ $? -eq 0 ]; then
    echo "¡Compilación exitosa!"
    echo ""
    echo "=== Ejecutando con 4 procesos MPI para desencriptar mensaje encriotado en .bin==="
    mpirun -np 4 ./main encrypted_output.bin "ipsum dolor sit amet"
else
    echo "Error en la compilación"
    exit 1
fi
