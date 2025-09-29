#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <openssl/des.h>
#include <time.h>

void decrypt(long key, unsigned char *ciph, int len, unsigned char *output){
    DES_cblock keyBlock;
    DES_key_schedule schedule;
    
    //Convert 56-bit key to 64-bit DES key with parity
    long k = 0;
    for(int i=0; i<8; ++i){
        key <<= 1;
        k += (key & (0xFEL << i*8));
    }
    
    //Copy key to DES_cblock
    memcpy(&keyBlock, &k, 8);
    DES_set_odd_parity(&keyBlock);
    DES_set_key_unchecked(&keyBlock, &schedule);
    
    //Decrypt each 8-byte block
    for(int i=0; i<len; i+=8){
        DES_ecb_encrypt((DES_cblock *)(ciph + i), 
                       (DES_cblock *)(output + i), 
                       &schedule, 
                       DES_DECRYPT);
    }
}

void encrypt(long key, unsigned char *plain, int len, unsigned char *output){
    DES_cblock keyBlock;
    DES_key_schedule schedule;
    
    //Convert 56-bit key to 64-bit DES key with parity
    long k = 0;
    for(int i=0; i<8; ++i){
        key <<= 1;
        k += (key & (0xFEL << i*8));
    }
    
    //Copy key to DES_cblock
    memcpy(&keyBlock, &k, 8);
    DES_set_odd_parity(&keyBlock);
    DES_set_key_unchecked(&keyBlock, &schedule);
    
    //Encrypt each 8-byte block
    for(int i=0; i<len; i+=8){
        DES_ecb_encrypt((DES_cblock *)(plain + i), 
                       (DES_cblock *)(output + i), 
                       &schedule, 
                       DES_ENCRYPT);
    }
}

int tryKey(long key, unsigned char *ciph, int len, char *search){
    unsigned char temp[len+1];
    decrypt(key, ciph, len, temp);
    temp[len] = 0;
    return strstr((char *)temp, search) != NULL;
}

//Read input file with 3 lines
int readInputFile(char *filename, long *key, char **plaintext, int *plainlen, char **search){
    FILE *file = fopen(filename, "r");
    if(!file){
        printf("Error: Cannot open file %s\n", filename);
        return 0;
    }
    
    //Line 1: encryption key
    if(fscanf(file, "%ld\n", key) != 1){
        printf("Error: Cannot read encryption key\n");
        fclose(file);
        return 0;
    }
    
    //Line 2: text to encrypt
    char buffer[1024];
    if(fgets(buffer, sizeof(buffer), file) == NULL){
        printf("Error: Cannot read plaintext\n");
        fclose(file);
        return 0;
    }
    //Remove newline
    buffer[strcspn(buffer, "\n")] = 0;
    
    //Pad to multiple of 8 bytes
    int len = strlen(buffer);
    int padded_len = ((len + 7) / 8) * 8;
    *plaintext = (char *)malloc(padded_len);
    memset(*plaintext, 0, padded_len);
    memcpy(*plaintext, buffer, len);
    *plainlen = padded_len;
    
    //Line 3: search substring
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

int main(int argc, char *argv[]){
    int N, id;
    long upper = (1L << 56); //upper bound DES keys 2^56
    long mylower, myupper;
    MPI_Status st;
    MPI_Request req;
    int ciphlen;
    MPI_Comm comm = MPI_COMM_WORLD;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_size(comm, &N);
    MPI_Comm_rank(comm, &id);
    
    if(argc != 2){
        if(id == 0){
            printf("Usage: mpirun -np <N> %s <input_file>\n", argv[0]);
            printf("Input file format:\n");
            printf("  Line 1: Encryption key (integer)\n");
            printf("  Line 2: Text to encrypt\n");
            printf("  Line 3: Substring to search for\n");
        }
        MPI_Finalize();
        return 1;
    }
    
    long encryption_key;
    char *plaintext = NULL;
    char *search = NULL;
    unsigned char *cipher = NULL;
    
    //Only rank 0 reads file and encrypts
    if(id == 0){
        printf("=== DES Brute Force Cracker ===\n");
        printf("Reading input from: %s\n\n", argv[1]);
        
        if(!readInputFile(argv[1], &encryption_key, &plaintext, &ciphlen, &search)){
            MPI_Abort(comm, 1);
        }
        
        printf("--- Input Parameters ---\n");
        printf("Encryption key: %ld\n", encryption_key);
        printf("Plaintext: %s\n", plaintext);
        printf("Plaintext length (padded): %d bytes\n", ciphlen);
        printf("Search string: \"%s\"\n", search);
        
        //Encrypt the plaintext
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
    }
    
    //Broadcast necessary data to all processes
    MPI_Bcast(&ciphlen, 1, MPI_INT, 0, comm);
    
    if(id != 0){
        cipher = (unsigned char *)malloc(ciphlen);
        search = (char *)malloc(256);
    }
    
    MPI_Bcast(cipher, ciphlen, MPI_UNSIGNED_CHAR, 0, comm);
    MPI_Bcast(search, 256, MPI_CHAR, 0, comm);
    
    //Divide search space among processes
    long range_per_node = upper / N;
    mylower = range_per_node * id;
    myupper = range_per_node * (id+1) - 1;
    if(id == N-1){
        myupper = upper;
    }
    
    if(id == 0){
        printf("--- Brute Force Search ---\n");
        printf("Total processes: %d\n", N);
        printf("Search space: 2^56 = %ld keys\n", upper);
        printf("Keys per process: ~%ld\n", range_per_node);
        printf("Starting search...\n\n");
    }
    
    long found = 0;
    MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);
    
    time_t start_time = time(NULL);
    long keys_tested = 0;
    int flag = 0;
    
    for(long i = mylower; i < myupper; ++i){
        //Check if another process found the key
        MPI_Test(&req, &flag, &st);
        if(flag && found != 0){
            printf("[Process %d] Received termination signal. Key found by another process: %ld\n", id, found);
            break;
        }
        
        if(tryKey(i, cipher, ciphlen, search)){
            found = i;
            printf("[Process %d] KEY FOUND: %ld\n", id, found);
            for(int node=0; node<N; node++){
                MPI_Send(&found, 1, MPI_LONG, node, 0, comm);
            }
            break;
        }
        keys_tested++;
        
        //Progress report every 1M keys (only for process 0)
        if(id == 0 && keys_tested % 1000000 == 0){
            double elapsed = difftime(time(NULL), start_time);
            if(elapsed > 0){
                printf("[Process %d] Progress: %ld keys tested (%.2f keys/sec)\n", 
                       id, keys_tested, keys_tested/elapsed);
            }
        }
    }
    
    //Cancel pending receive if not completed
    if(!flag){
        MPI_Cancel(&req);
        MPI_Wait(&req, &st);
    }
    
    if(id == 0){
        //Wait for result if not already received
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