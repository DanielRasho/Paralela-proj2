## Proyecto 2 - DES Cracker

```bash
# Compilar
mpicc -o main program.c -lssl -lcrypto
# Ejecutar
mpirun -np 4 ./bruteforce input.txt  
```

Input text file
```
234513          <= encryption key
Hello world     <= text to encrypt
Hello           <= substring to search
```