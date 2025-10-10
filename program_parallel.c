/**
 * @file program_parallel.c
 * @brief Hybrid MPI+OpenMP DES encryption/decryption and brute-force cracker
 *
 * This program supports two modes:
 * 1. Encryption mode: Encrypts text from input file and saves to binary file
 * 2. Brute-force mode: Decrypts binary file using parallel keyspace search
 *
 * Uses MPI for distributed processing and OpenMP for shared-memory parallelism
 * to achieve maximum performance when searching the DES keyspace (2^56 keys).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>
#include <openssl/des.h>
#include <time.h>

/**
 * @brief Decrypts ciphertext using DES algorithm (OpenSSL implementation)
 *
 * Converts a 56-bit key to a 64-bit DES key with parity bits and performs
 * DES decryption in ECB mode using OpenSSL's DES functions.
 *
 * @param key 56-bit DES key (without parity bits)
 * @param ciph Pointer to ciphertext buffer
 * @param len Length of the ciphertext (must be multiple of 8)
 * @param output Pointer to output buffer for decrypted data
 */
void decrypt(long key, unsigned char *ciph, int len, unsigned char *output){
    DES_cblock keyBlock;
    DES_key_schedule schedule;

    // Convert 56-bit key to 64-bit DES key with parity
    long k = 0;
    for(int i=0; i<8; ++i){
        key <<= 1;
        k += (key & (0xFEL << i*8));
    }

    // Copy key to DES_cblock and set parity
    memcpy(&keyBlock, &k, 8);
    DES_set_odd_parity(&keyBlock);
    DES_set_key_unchecked(&keyBlock, &schedule);

    // Decrypt data in 8-byte blocks
    for(int i=0; i<len; i+=8){
        DES_ecb_encrypt((DES_cblock *)(ciph + i),
                       (DES_cblock *)(output + i),
                       &schedule,
                       DES_DECRYPT);
    }
}

/**
 * @brief Encrypts plaintext using DES algorithm (OpenSSL implementation)
 *
 * Converts a 56-bit key to a 64-bit DES key with parity bits and performs
 * DES encryption in ECB mode using OpenSSL's DES functions.
 *
 * @param key 56-bit DES key (without parity bits)
 * @param plain Pointer to plaintext buffer
 * @param len Length of the plaintext (must be multiple of 8)
 * @param output Pointer to output buffer for encrypted data
 */
void encrypt(long key, unsigned char *plain, int len, unsigned char *output){
    DES_cblock keyBlock;
    DES_key_schedule schedule;

    // Convert 56-bit key to 64-bit DES key with parity
    long k = 0;
    for(int i=0; i<8; ++i){
        key <<= 1;
        k += (key & (0xFEL << i*8));
    }

    memcpy(&keyBlock, &k, 8);
    DES_set_odd_parity(&keyBlock);
    DES_set_key_unchecked(&keyBlock, &schedule);

    // Encrypt data in 8-byte blocks
    for(int i=0; i<len; i+=8){
        DES_ecb_encrypt((DES_cblock *)(plain + i),
                       (DES_cblock *)(output + i),
                       &schedule,
                       DES_ENCRYPT);
    }
}

/**
 * @brief Tests if a key successfully decrypts the ciphertext
 *
 * Attempts decryption with the given key and checks if the resulting
 * plaintext contains the search pattern.
 *
 * @param key Candidate DES key to test
 * @param ciph Ciphertext to decrypt
 * @param len Length of the ciphertext
 * @param search Search string to look for in decrypted text
 * @return 1 if the decrypted text contains the search pattern, 0 otherwise
 */
int tryKey(long key, unsigned char *ciph, int len, char *search){
    unsigned char temp[len+1];
    decrypt(key, ciph, len, temp);
    temp[len] = 0;
    return strstr((char *)temp, search) != NULL;
}

/**
 * @brief Reads encrypted data from a binary file
 *
 * Opens and reads the entire contents of a binary file into memory.
 *
 * @param filename Path to the binary file to read
 * @param cipher Pointer to store allocated buffer containing file data
 * @param ciphlen Pointer to store the length of the data read
 * @return 1 on success, 0 on failure
 */
int readEncryptedFile(char *filename, unsigned char **cipher, int *ciphlen){
    FILE *file = fopen(filename, "rb");
    if(!file){
        printf("Error: Cannot open file %s\n", filename);
        return 0;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    *ciphlen = filesize;
    *cipher = (unsigned char *)malloc(filesize);

    if(fread(*cipher, 1, filesize, file) != filesize){
        printf("Error: Cannot read encrypted file\n");
        fclose(file);
        free(*cipher);
        return 0;
    }

    fclose(file);
    return 1;
}

/**
 * @brief Reads encryption parameters from input file
 *
 * Reads a 3-line input file with format:
 * Line 1: Encryption key (integer)
 * Line 2: Text to encrypt
 * Line 3: Search substring (optional, for verification)
 *
 * The plaintext is padded to a multiple of 8 bytes for DES encryption.
 *
 * @param filename Path to the input file
 * @param key Pointer to store the encryption key
 * @param plaintext Pointer to store allocated and padded plaintext buffer
 * @param plainlen Pointer to store the padded plaintext length
 * @param search Pointer to store the search substring (may be NULL)
 * @return 1 on success, 0 on failure
 */
int readInputFile(char *filename, long *key, char **plaintext, int *plainlen, char **search){
    FILE *file = fopen(filename, "r");
    if(!file){
        printf("Error: Cannot open file %s\n", filename);
        return 0;
    }

    if(fscanf(file, "%ld\n", key) != 1){
        printf("Error: Cannot read encryption key\n");
        fclose(file);
        return 0;
    }

    char buffer[1024];
    if(fgets(buffer, sizeof(buffer), file) == NULL){
        printf("Error: Cannot read plaintext\n");
        fclose(file);
        return 0;
    }
    buffer[strcspn(buffer, "\n")] = 0;

    // Pad to multiple of 8 bytes for DES block size
    int len = strlen(buffer);
    int padded_len = ((len + 7) / 8) * 8;
    *plaintext = (char *)malloc(padded_len);
    memset(*plaintext, 0, padded_len);
    memcpy(*plaintext, buffer, len);
    *plainlen = padded_len;

    // Line 3: search substring (optional)
    if(fgets(buffer, sizeof(buffer), file) != NULL){
        buffer[strcspn(buffer, "\n")] = 0;
        *search = strdup(buffer);
    } else {
        // If no search string provided
        *search = NULL;
    }

    fclose(file);
    return 1;
}

/**
 * @brief Main entry point for DES encryption/brute-force program
 *
 * Supports two modes of operation:
 * 1. Encryption mode: Reads plaintext from .txt file and encrypts to .bin file
 * 2. Brute-force mode: Reads encrypted .bin file and searches for decryption key
 *
 * In brute-force mode, uses hybrid MPI+OpenMP parallelism to distribute the
 * keyspace search across processes and threads.
 *
 * @param argc Argument count
 * @param argv Argument vector
 *   Encryption mode: program <input.txt> <output.bin>
 *   Brute-force mode: program <encrypted.bin> <search_string>
 * @return 0 on success, 1 on error
 */
int main(int argc, char *argv[]){
    int N, id;
    long upper = (1L << 56); // Upper bound for DES keys: 2^56
    long mylower, myupper;
    MPI_Status st;
    MPI_Request req;
    int ciphlen;
    MPI_Comm comm = MPI_COMM_WORLD;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(comm, &N);
    MPI_Comm_rank(comm, &id);

    // Determine mode based on arguments
    int encrypt_mode = 0; // 0 = brute force mode, 1 = encrypt mode

    if(argc == 3){
        // Mode: Encrypt from input.txt
        char *filename = argv[1];
        char *output_bin = argv[2];

        // Check if first argument is a .txt file (encrypt mode)
        if(strstr(filename, ".txt") != NULL){
            encrypt_mode = 1;

            if(id == 0){
                printf("=== DES Encryption Mode ===\n");

                long encryption_key;
                char *plaintext = NULL;
                char *search = NULL;

                if(!readInputFile(filename, &encryption_key, &plaintext, &ciphlen, &search)){
                    MPI_Abort(comm, 1);
                }

                printf("Input file: %s\n", filename);
                printf("Encryption key: %ld\n", encryption_key);
                printf("Plaintext: %s\n", plaintext);
                printf("Plaintext length (padded): %d bytes\n", ciphlen);
                printf("Output file: %s\n\n", output_bin);

                unsigned char *cipher = (unsigned char *)malloc(ciphlen);
                encrypt(encryption_key, (unsigned char *)plaintext, ciphlen, cipher);

                //Write to binary file
                FILE *file = fopen(output_bin, "wb");
                if(!file){
                    printf("Error: Cannot create file %s\n", output_bin);
                    MPI_Abort(comm, 1);
                }

                fwrite(cipher, 1, ciphlen, file);
                fclose(file);

                printf("--- Encryption Complete ---\n");
                printf("Ciphertext (hex): ");
                for(int i=0; i<ciphlen && i<32; i++){
                    printf("%02x ", cipher[i]);
                }
                if(ciphlen > 32) printf("...");
                printf("\n\n");
                printf("File saved: %s\n", output_bin);
                if(search != NULL){
                    printf("Search string for decryption: \"%s\"\n", search);
                }

                free(cipher);
                free(plaintext);
                if(search) free(search);
            }

            MPI_Finalize();
            return 0;
        }
    }

    if(argc != 3 || encrypt_mode){
        if(id == 0){
            printf("Usage:\n");
            printf("  Encrypt mode:\n");
            printf("    mpirun -np <N> %s <input.txt> <output.bin>\n", argv[0]);
            printf("    input.txt format:\n");
            printf("      Line 1: Encryption key (integer)\n");
            printf("      Line 2: Text to encrypt\n");
            printf("      Line 3: Search substring (for verification)\n");
            printf("\n");
            printf("  Brute force mode:\n");
            printf("    mpirun -np <N> %s <encrypted.bin> <search_string>\n", argv[0]);
            printf("    encrypted.bin: Binary file with encrypted data\n");
            printf("    search_string: Text fragment to search for\n");
        }
        MPI_Finalize();
        return 1;
    }

    // Brute force mode
    char *search = NULL;
    unsigned char *cipher = NULL;

    // Only rank 0 reads encrypted file
    if(id == 0){
        printf("=== DES Brute Force Cracker (MPI + OpenMP) ===\n");
        printf("Encrypted file: %s\n", argv[1]);
        printf("Search string: \"%s\"\n\n", argv[2]);

        if(!readEncryptedFile(argv[1], &cipher, &ciphlen)){
            MPI_Abort(comm, 1);
        }

        search = strdup(argv[2]);

        printf("--- Encrypted Data ---\n");
        printf("Ciphertext length: %d bytes\n", ciphlen);
        printf("Ciphertext (hex): ");
        for(int i=0; i<ciphlen && i<32; i++){
            printf("%02x ", cipher[i]);
        }
        if(ciphlen > 32) printf("...");
        printf("\n\n");
    }

    // Broadcast ciphertext length to all processes
    MPI_Bcast(&ciphlen, 1, MPI_INT, 0, comm);

    if(id != 0){
        cipher = (unsigned char *)malloc(ciphlen);
        search = (char *)malloc(256);
    }

    // Broadcast ciphertext and search string to all processes
    MPI_Bcast(cipher, ciphlen, MPI_UNSIGNED_CHAR, 0, comm);
    MPI_Bcast(search, 256, MPI_CHAR, 0, comm);

    // Divide keyspace among MPI processes
    long range_per_node = upper / N;
    mylower = range_per_node * id;
    myupper = range_per_node * (id+1) - 1;
    if(id == N-1){
        // Last process handles remainder
        myupper = upper;
    }

    if(id == 0){
        printf("--- Brute Force Search ---\n");
        printf("Total processes: %d\n", N);
        printf("Search space: 2^56 = %ld keys\n", upper);
        printf("Keys per process: ~%ld\n", range_per_node);
        printf("Starting search...\n\n");
    }

    int num_threads = omp_get_max_threads();
    printf("[Process %d] Searching range: %ld to %ld with %d OpenMP threads\n",
           id, mylower, myupper, num_threads);

    long found = 0;
    // Set up non-blocking receive to detect when another process finds the key
    MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

    time_t start_time = time(NULL);
    long keys_tested = 0;
    int flag = 0;

    // Parallel key search using OpenMP threads within each MPI process
    #pragma omp parallel shared(found, cipher, ciphlen, search, req, flag, st)
    {
        int thread_id = omp_get_thread_num();
        int total_threads = omp_get_num_threads();
        long local_keys_tested = 0;

        // Each thread gets its own portion of the process's keyspace range
        long range_size = myupper - mylower;
        long keys_per_thread = range_size / total_threads;
        long thread_lower = mylower + thread_id * keys_per_thread;
        long thread_upper = (thread_id == total_threads - 1) ? myupper : thread_lower + keys_per_thread;

        for(long i = thread_lower; i < thread_upper; ++i){
            // Check if key was found by any thread (shared variable)
            int local_found = 0;
            #pragma omp atomic read
            local_found = found;

            if(local_found != 0){
                break;
            }

            // Check if key was found by another process (only master thread checks MPI)
            if(thread_id == 0 && local_keys_tested % 10000 == 0){
                MPI_Test(&req, &flag, &st);
                if(flag && found != 0){
                    break;
                }
            }

            // Try current key
            if(tryKey(i, cipher, ciphlen, search)){
                #pragma omp critical
                {
                    if(found == 0){
                        found = i;
                        printf("[Process %d, Thread %d] KEY FOUND: %ld\n", id, thread_id, found);
                        // Notify all MPI processes that key was found
                        for(int node=0; node<N; node++){
                            MPI_Send(&found, 1, MPI_LONG, node, 0, comm);
                        }
                    }
                }
                break;
            }
            local_keys_tested++;

            // Update global counter periodically to track progress
            if(local_keys_tested % 100000 == 0){
                #pragma omp atomic
                keys_tested += 100000;
                local_keys_tested = 0;
            }

            // Print progress updates (only master thread)
            if(thread_id == 0 && i % 1000000 == 0 && i > thread_lower){
                double elapsed = difftime(time(NULL), start_time);
                if(elapsed > 0){
                    long total_tested;
                    #pragma omp atomic read
                    total_tested = keys_tested;
                    printf("[Process %d] Progress: %ld keys tested (%.2f keys/sec)\n",
                           id, total_tested, total_tested/elapsed);
                }
            }
        }

        // Add remaining keys to global counter
        #pragma omp atomic
        keys_tested += local_keys_tested;
    }

    if(!flag){
        MPI_Cancel(&req);
        MPI_Wait(&req, &st);
    }

    if(id == 0){
        if(!flag){
            MPI_Wait(&req, &st);
        }
        time_t end_time = time(NULL);

        printf("\n=== Results ===\n");
        if(found > 0){
            unsigned char decrypted[ciphlen+1];
            decrypt(found, cipher, ciphlen, decrypted);
            decrypted[ciphlen] = 0;

            printf("SUCCESS!\n");
            printf("Key found: %ld\n", found);
            printf("Decrypted text: %s\n", decrypted);
            printf("Time elapsed: %.2f seconds\n", difftime(end_time, start_time));
        } else {
            printf("FAILED - Key not found in search space\n");
        }
    }

    free(cipher);
    if(search) free(search);

    MPI_Finalize();
    return 0;
}
