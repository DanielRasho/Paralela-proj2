## Proyecto 2 - DES Cracker

```bash
# Compilar
mpicc -o main program.c -lssl -lcrypto
# Ejecutar
mpirun -np 4 ./bruteforce input.txt  
```