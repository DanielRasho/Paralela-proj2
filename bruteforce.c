/**
 * @file bruteforce.c
 * @brief Parallel DES brute-force decryption using MPI
 *
 * This program performs distributed brute-force attack on DES-encrypted data
 * using MPI for parallel processing. Each MPI process searches a portion of
 * the keyspace (2^56 possible keys) until the correct key is found.
 *
 * Note: The key used is quite small; with random keys, speedup will vary.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
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
 * @brief Main entry point for parallel DES brute-force cracker
 *
 * Initializes MPI, distributes keyspace among processes, and performs
 * parallel search for the correct decryption key. The first process to
 * find the key notifies all other processes to terminate.
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

  // Divide keyspace among processes
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

  // Search assigned keyspace
  for(int i = mylower; i<myupper && (found==0); ++i){
    if(tryKey(i, (char *)cipher, ciphlen)){
      found = i;
      // Notify all processes that key was found
      for(int node=0; node<N; node++){
        MPI_Send(&found, 1, MPI_LONG, node, 0, MPI_COMM_WORLD);
      }
      break;
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
