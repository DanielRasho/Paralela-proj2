#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>
#include <unistd.h>
#include <rpc/des_crypt.h>

void decrypt(long key, char *ciph, int len){
  //set parity of key and do decrypt
  long k = 0;
  for(int i=0; i<8; ++i){
    key <<= 1;
    k += (key & (0xFE << i*8));
  }
  des_setparity((char *)&k);  //el poder del casteo y &
  ecb_crypt((char *)&k, (char *) ciph, 16, DES_DECRYPT);
}

void encrypt(long key, char *ciph, int len){
  //set parity of key and do decrypt
  long k = 0;
  for(int i=0; i<8; ++i){
    key <<= 1;
    k += (key & (0xFE << i*8));
  }
  des_setparity((char *)&k);  //el poder del casteo y &
  ecb_crypt((char *)&k, (char *) ciph, 16, DES_ENCRYPT);
}

char search[] = " the ";
int tryKey(long key, char *ciph, int len){
  char temp[len+1];
  memcpy(temp, ciph, len);
  temp[len]=0;
  decrypt(key, temp, len);
  return strstr((char *)temp, search) != NULL;
}

unsigned char cipher[] = {108, 245, 65, 63, 125, 200, 150, 66, 17, 170, 207, 170, 34, 31, 70, 215, 0};
int main(int argc, char *argv[]){ //char **argv
  int N, id;
  long upper = (1L <<56); //upper bound DES keys 2^56
  long mylower, myupper;
  MPI_Status st;
  MPI_Request req;
  int flag;
  int ciphlen = strlen(cipher);
  MPI_Comm comm = MPI_COMM_WORLD;

  MPI_Init(NULL, NULL);
  MPI_Comm_size(comm, &N);
  MPI_Comm_rank(comm, &id);

  int range_per_node = upper / N;
  mylower = range_per_node * id;
  myupper = range_per_node * (id+1) -1;
  if(id == N-1){
    //compensar residuo
    myupper = upper;
  }

  long found = 0;

  MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

  int num_threads = omp_get_max_threads();
  if(id == 0){
    printf("MPI Processes: %d, OpenMP Threads per process: %d\n", N, num_threads);
  }

  //Parallel key search using OpenMP
  #pragma omp parallel shared(found, cipher, ciphlen, req, flag, st)
  {
    int thread_id = omp_get_thread_num();
    int total_threads = omp_get_num_threads();

    //Each thread gets its own portion of the range
    long range_size = myupper - mylower;
    long keys_per_thread = range_size / total_threads;
    long thread_lower = mylower + thread_id * keys_per_thread;
    long thread_upper = (thread_id == total_threads - 1) ? myupper : thread_lower + keys_per_thread;

    for(long i = thread_lower; i < thread_upper; ++i){
      //Check if key was found (shared variable)
      long local_found = 0;
      #pragma omp atomic read
      local_found = found;

      if(local_found != 0){
        break;
      }

      //Check if key was found by another process (only master thread checks MPI)
      if(thread_id == 0 && i % 10000 == 0){
        MPI_Test(&req, &flag, &st);
        if(flag && found != 0){
          break;
        }
      }

      if(tryKey(i, (char *)cipher, ciphlen)){
        #pragma omp critical
        {
          if(found == 0){
            found = i;
            printf("[Process %d, Thread %d] KEY FOUND: %ld\n", id, thread_id, found);
            for(int node=0; node<N; node++){
              MPI_Send(&found, 1, MPI_LONG, node, 0, MPI_COMM_WORLD);
            }
          }
        }
        break;
      }
    }
  }

  if(id==0){
    MPI_Wait(&req, &st);
    decrypt(found, (char *)cipher, ciphlen);
    printf("%li %s\n", found, cipher);
  }

  MPI_Finalize();
}
