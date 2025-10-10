/**
 * @file program.c
 * @brief MPI-based DES encryption/decryption and brute-force cracker
 *
 * This program supports two modes:
 * 1. Encryption mode: Encrypts text from input file and saves to binary file
 * 2. Brute-force mode: Decrypts binary file using parallel keyspace search with MPI
 *
 * Uses MPI for distributed processing to search the DES keyspace (2^56 keys).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
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
 * @brief Saves encrypted data to a binary file
 *
 * @param filename Path to the output file
 * @param data Pointer to data to write
 * @param len Length of data in bytes
 */
void saveBinaryFile(const char *filename, unsigned char *data, int len){
    FILE *file = fopen(filename, "wb");
    if(!file){
        printf("Error: Cannot create file %s\n", filename);
        return;
    }
    fwrite(data, 1, len, file);
    fclose(file);
    printf("Encrypted data saved to: %s\n", filename);
}

/**
 * @brief Reads a binary file into memory
 *
 * @param filename Path to the binary file to read
 * @param data Pointer to store allocated buffer containing file data
 * @param len Pointer to store the length of the data read
 * @return 1 on success, 0 on failure
 */
int readBinaryFile(const char *filename, unsigned char **data, int *len){
    FILE *file = fopen(filename, "rb");
    if(!file){
        printf("Error: Cannot open file %s\n", filename);
        return 0;
    }

    fseek(file, 0, SEEK_END);
    *len = ftell(file);
    fseek(file, 0, SEEK_SET);

    *data = (unsigned char *)malloc(*len);
    fread(*data, 1, *len, file);
    fclose(file);

    printf("Loaded encrypted file: %s (%d bytes)\n", filename, *len);
    return 1;
}

/**
 * @brief Reads encryption parameters from input file
 *
 * Reads a 3-line input file with format:
 * Line 1: Encryption key (integer)
 * Line 2: Text to encrypt
 * Line 3: Search substring (for verification)
 *
 * The plaintext is padded to a multiple of 8 bytes for DES encryption.
 *
 * @param filename Path to the input file
 * @param key Pointer to store the encryption key
 * @param plaintext Pointer to store allocated and padded plaintext buffer
 * @param plainlen Pointer to store the padded plaintext length
 * @param search Pointer to store the search substring
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

    // Line 3: search substring
    if(fgets(buffer, sizeof(buffer), file) == NULL){
        printf("Error: Cannot read search string\n");
        fclose(file);
        free(*plaintext);
        return 0;
    }
    buffer[strcspn(buffer, "\n")] = 0;
    *search = strdup(buffer);

    fclose(file);
    return 1;
}

/**
 * @brief Checks if a filename has a .bin extension
 *
 * @param filename Filename to check
 * @return 1 if filename ends with .bin, 0 otherwise
 */
int isBinaryFile(const char *filename){
    int len = strlen(filename);
    return (len > 4 && strcmp(filename + len - 4, ".bin") == 0);
}

/**
 * @brief Main entry point for DES encryption/brute-force program
 *
 * Supports two modes of operation:
 * 1. Encryption mode: Reads plaintext from .txt file and encrypts to .bin file
 * 2. Brute-force mode: Reads encrypted .bin file and searches for decryption key
 *
 * In brute-force mode, uses MPI parallelism to distribute the keyspace search
 * across multiple processes.
 *
 * @param argc Argument count
 * @param argv Argument vector
 *   Encryption mode: program <input.txt>
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

    if(argc < 2){
        if(id == 0){
            printf("Usage:\n");
            printf("  MODE 1 (Encrypt from .txt):\n");
            printf("    mpirun -np <N> %s <input.txt>\n", argv[0]);
            printf("    Input file format:\n");
            printf("      Line 1: Encryption key (integer)\n");
            printf("      Line 2: Text to encrypt\n");
            printf("      Line 3: Substring to search for\n");
            printf("\n");
            printf("  MODE 2 (Decrypt from .bin):\n");
            printf("    mpirun -np <N> %s <encrypted.bin> <search_string>\n", argv[0]);
            printf("    Example: mpirun -np 4 %s message.bin \"secret message\"\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }
    
    long encryption_key;
    char *plaintext = NULL;
    char *search = NULL;
    unsigned char *cipher = NULL;
    int is_binary_mode = isBinaryFile(argv[1]);

    // Only rank 0 reads file and encrypts (encryption mode)
    if(!is_binary_mode){
        if(id == 0){
            printf("=== MODE 1: DES Brute Force Cracker ===\n");
            printf("Reading input from: %s\n\n", argv[1]);
            
            if(!readInputFile(argv[1], &encryption_key, &plaintext, &ciphlen, &search)){
                MPI_Abort(comm, 1);
            }
            
            printf("--- Input Parameters ---\n");
            printf("Encryption key: %ld\n", encryption_key);
            printf("Plaintext: %s\n", plaintext);
            printf("Plaintext length (padded): %d bytes\n", ciphlen);
            printf("Search string: \"%s\"\n", search);
            
            cipher = (unsigned char *)malloc(ciphlen);
            encrypt(encryption_key, (unsigned char *)plaintext, ciphlen, cipher);
            
            printf("\n--- Encrypted Data ---\n");
            printf("Ciphertext (array): {");
            for(int i=0; i<ciphlen; i++){
                printf("%d", cipher[i]);
                if(i < ciphlen-1) printf(", ");
            }
            printf("}\n");
            
            printf("Ciphertext (hex): ");
            for(int i=0; i<ciphlen; i++){
                printf("%02x ", cipher[i]);
            }
            printf("\n");
            
            printf("Ciphertext (text): ");
            for(int i=0; i<ciphlen; i++){
                if(cipher[i] >= 32 && cipher[i] <= 126){
                    printf("%c", cipher[i]);
                } else {
                    printf(".");
                }
            }
            printf("\n\n");

            char bin_filename[256];
            snprintf(bin_filename, sizeof(bin_filename), "encrypted_output.bin");
            saveBinaryFile(bin_filename, cipher, ciphlen);
            printf("\n");
        }
    } else {
        // Binary file mode - brute force decryption
        if(argc < 3){
            if(id == 0){
                printf("Error: Search string required for .bin mode\n");
                printf("Usage: mpirun -np <N> %s <file.bin> <search_string>\n", argv[0]);
            }
            MPI_Finalize();
            return 1;
        }
    
        if(id == 0){
            printf("=== MODE 2: Decrypt from Binary ===\n");
            printf("Encrypted file: %s\n", argv[1]);
            printf("Search string: \"%s\"\n\n", argv[2]);
            
            if(!readBinaryFile(argv[1], &cipher, &ciphlen)){
                MPI_Abort(comm, 1);
            }
            
            search = strdup(argv[2]);
            
            printf("Ciphertext (hex): ");
            for(int i=0; i<ciphlen; i++){
                printf("%02x ", cipher[i]);
            }
            printf("\n\n");
        }
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
    
    printf("[Process %d] Searching range: %ld to %ld\n", id, mylower, myupper);

    long found = 0;
    // Set up non-blocking receive to detect when another process finds the key
    MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

    time_t start_time = time(NULL);
    long keys_tested = 0;
    int flag = 0;

    // Search assigned keyspace range
    // Check if another process found the key (check every 10000 keys to reduce overhead)
    for(long i = mylower; i < myupper; ++i){
        if(keys_tested % 10000 == 0){
            MPI_Test(&req, &flag, &st);
            if(flag && found != 0){
                printf("[Process %d] Received termination signal. Key found by another process: %ld\n", id, found);
                break;
            }
        }

        // Try current key
        if(tryKey(i, cipher, ciphlen, search)){
            found = i;
            printf("[Process %d] KEY FOUND: %ld\n", id, found);
            // Notify all processes that key was found
            for(int node=0; node<N; node++){
                MPI_Send(&found, 1, MPI_LONG, node, 0, comm);
            }
            break;
        }
        keys_tested++;

        // Print progress updates periodically
        if(keys_tested % 1000000 == 0){
            double elapsed = difftime(time(NULL), start_time);
            if(elapsed > 0){
                printf("[Process %d] Progress: %ld keys tested (%.2f keys/sec)\n",
                       id, keys_tested, keys_tested/elapsed);
            }
        }
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
    if(id == 0 && plaintext) free(plaintext);
    if(search) free(search);
    
    MPI_Finalize();
    return 0;
}