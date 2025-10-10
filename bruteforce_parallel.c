/**
 * @file bruteforce_parallel.c
 * @brief Hybrid MPI+OpenMP parallel DES brute-force decryption
 *
 * This program combines MPI for distributed processing and OpenMP for
 * shared-memory parallelism to perform brute-force attack on DES-encrypted
 * data. The keyspace (2^56 keys) is divided among MPI processes, and each
 * process uses multiple OpenMP threads to search its assigned range.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>
#include <unistd.h>
#include <rpc/des_crypt.h>

/**
 * @brief Decrypts ciphertext using DES algorithm
 *
 * Sets the parity bits of the key and performs DES decryption in ECB mode.
 *
 * @param key 56-bit DES key (without parity bits)
 * @param ciph Pointer to ciphertext buffer (will be modified in-place)
 * @param len Length of the ciphertext
 */
void decrypt(long key, char *ciph, int len){
  // Set parity of key and do decrypt
  long k = 0;
  for(int i=0; i<8; ++i){
    key <<= 1;
    k += (key & (0xFE << i*8));
  }
  des_setparity((char *)&k);
  ecb_crypt((char *)&k, (char *) ciph, 16, DES_DECRYPT);
}

/**
 * @brief Encrypts plaintext using DES algorithm
 *
 * Sets the parity bits of the key and performs DES encryption in ECB mode.
 *
 * @param key 56-bit DES key (without parity bits)
 * @param ciph Pointer to plaintext buffer (will be modified in-place)
 * @param len Length of the plaintext
 */
void encrypt(long key, char *ciph, int len){
  // Set parity of key and do encrypt
  long k = 0;
  for(int i=0; i<8; ++i){
    key <<= 1;
    k += (key & (0xFE << i*8));
  }
  des_setparity((char *)&k);
  ecb_crypt((char *)&k, (char *) ciph, 16, DES_ENCRYPT);
}

/** Search pattern to identify successful decryption */
char search[] = " the ";

/**
 * @brief Tests if a key successfully decrypts the ciphertext
 *
 * Attempts decryption with the given key and checks if the resulting
 * plaintext contains the search pattern.
 *
 * @param key Candidate DES key to test
 * @param ciph Ciphertext to decrypt
 * @param len Length of the ciphertext
 * @return 1 if the decrypted text contains the search pattern, 0 otherwise
 */
int tryKey(long key, char *ciph, int len){
  char temp[len+1];
  memcpy(temp, ciph, len);
  temp[len]=0;
  decrypt(key, temp, len);
  return strstr((char *)temp, search) != NULL;
}

/** Hardcoded encrypted message to crack */
unsigned char cipher[] = {108, 245, 65, 63, 125, 200, 150, 66, 17, 170, 207, 170, 34, 31, 70, 215, 0};

/**
 * @brief Main entry point for hybrid MPI+OpenMP DES brute-force cracker
 *
 * Initializes MPI and OpenMP, distributes keyspace among MPI processes,
 * then uses OpenMP threads within each process to search in parallel.
 * The first thread/process to find the key notifies all others to terminate.
 *
 * @param argc Argument count (not used)
 * @param argv Argument vector (not used)
 * @return 0 on success
 */
int main(int argc, char *argv[]){
  int N, id;
  long upper = (1L <<56); // Upper bound for DES keys: 2^56
  long mylower, myupper;
  MPI_Status st;
  MPI_Request req;
  int flag;
  int ciphlen = strlen(cipher);
  MPI_Comm comm = MPI_COMM_WORLD;

  // Initialize MPI environment
  MPI_Init(NULL, NULL);
  MPI_Comm_size(comm, &N);
  MPI_Comm_rank(comm, &id);

  // Divide keyspace among MPI processes
  int range_per_node = upper / N;
  mylower = range_per_node * id;
  myupper = range_per_node * (id+1) -1;
  if(id == N-1){
    // Last process handles remainder
    myupper = upper;
  }

  long found = 0;

  // Set up non-blocking receive to detect when another process finds the key
  MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

  int num_threads = omp_get_max_threads();
  if(id == 0){
    printf("MPI Processes: %d, OpenMP Threads per process: %d\n", N, num_threads);
  }

  // Parallel key search using OpenMP within each MPI process
  #pragma omp parallel shared(found, cipher, ciphlen, req, flag, st)
  {
    int thread_id = omp_get_thread_num();
    int total_threads = omp_get_num_threads();

    // Each thread gets its own portion of the process's keyspace range
    long range_size = myupper - mylower;
    long keys_per_thread = range_size / total_threads;
    long thread_lower = mylower + thread_id * keys_per_thread;
    long thread_upper = (thread_id == total_threads - 1) ? myupper : thread_lower + keys_per_thread;

    for(long i = thread_lower; i < thread_upper; ++i){
      // Check if key was found by any thread (shared variable)
      long local_found = 0;
      #pragma omp atomic read
      local_found = found;

      if(local_found != 0){
        break;
      }

      // Check if key was found by another process (only master thread checks MPI)
      if(thread_id == 0 && i % 10000 == 0){
        MPI_Test(&req, &flag, &st);
        if(flag && found != 0){
          break;
        }
      }

      // Try current key
      if(tryKey(i, (char *)cipher, ciphlen)){
        #pragma omp critical
        {
          if(found == 0){
            found = i;
            printf("[Process %d, Thread %d] KEY FOUND: %ld\n", id, thread_id, found);
            // Notify all MPI processes that key was found
            for(int node=0; node<N; node++){
              MPI_Send(&found, 1, MPI_LONG, node, 0, MPI_COMM_WORLD);
            }
          }
        }
        break;
      }
    }
  }

  // Process 0 prints the result
  if(id==0){
    MPI_Wait(&req, &st);
    decrypt(found, (char *)cipher, ciphlen);
    printf("%li %s\n", found, cipher);
  }

  MPI_Finalize();
}
