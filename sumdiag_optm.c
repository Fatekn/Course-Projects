// optimized versions of matrix diagonal summing
#include "sumdiag.h"

////////////////////////////////////////////////////////////////////////////////
// REQUIRED: Paste a copy of your sumdiag_benchmark from an ODD grace
// node below.
//
// grace5:~/p5-code: ./sumdiag_benchmark
// ==== Matrix Diagonal Sum Benchmark Version 5 ====
// ------ Tuned for ODD grace.umd.edu machines -----
// Running with 5 sizes (max 8192) and 4 thread_counts (max 4)
//   SIZE   BASE #T   OPTM  SPDUP POINTS
//   1024  0.027  1  0.018   1.52   0.14
//                2  0.009   3.06   0.37
//                3  0.008   3.50   0.41
//                4  0.007   3.70   0.43
//   1101  0.030  1  0.019   1.53   0.15
//                2  0.010   2.94   0.38
//                3  0.007   4.11   0.50
//                4  0.008   3.70   0.46
//   2048  0.110  1  0.065   1.68   0.34
//                2  0.033   3.29   0.78
//                3  0.026   4.28   0.96
//                4  0.028   3.87   0.89
//   4099  0.476  1  0.268   1.77   0.75
//                2  0.135   3.52   1.65
//                3  0.092   5.18   2.16
//                4  0.099   4.80   2.06
//   8192  2.910  1  1.161   2.51   2.41
//                2  0.526   5.54   4.49
//                3  0.353   8.25   5.54
//                4  0.288  10.12   6.08
// RAW POINTS: 30.96
// TOTAL POINTS: 30 / 30


// You can write several different versions of your optimized function
// in this file and call one of them in the last function.

typedef struct {
  int thread_id;                // logical id of thread, 0,1,2,...
  int thread_count;             // total threads working on summing
  matrix_t mat;                 // matrix to sum
  vector_t vec;                 // vector to place sums
  pthread_mutex_t *vec_lock;    // mutex to lock the vec before adding on results
} sumdiag_context_t;

void* sumdiag_worker(void* arg);

int sumdiag_VER1(matrix_t mat, vector_t vec, int thread_count) {
  // OPTIMIZED CODE HERE
  if(vec.len != (mat.rows+mat.cols-1)) {
    return 1;
  }

  // initialize the shared results vector
  for(int j=0; j<vec.len; j++){
    VSET(vec,j,0);
  }

  // init a mutex to be used for threads to add on their local results
  pthread_mutex_t vec_lock;
  pthread_mutex_init(&vec_lock, NULL);

  pthread_t threads[thread_count];           // track each thread
  sumdiag_context_t ctxs[thread_count];      // context for each trhead

  // loop to create threads
  for(int i=0; i<thread_count; i++){

    // fill field values for the context to pass to threads
    ctxs[i].thread_id = i;
    ctxs[i].mat = mat;
    ctxs[i].vec = vec;
    ctxs[i].thread_count = thread_count;
    ctxs[i].vec_lock = &vec_lock;

    // Loop to create the threads
    pthread_create(&threads[i], NULL,        // start worker thread to compute part of answer
                   sumdiag_worker,
                   &ctxs[i]);
  }

  // use a loop to join the threads
  for(int i = 0; i < thread_count; i++){
    pthread_join(threads[i], NULL);
  }

  pthread_mutex_destroy(&vec_lock);
  return 0;
}

void *sumdiag_worker(void *arg){
  // extract the parameters / "context" via a cast
  sumdiag_context_t ctx = *((sumdiag_context_t *) arg);

  // extract the matrix and vector for the parameter context struct
  matrix_t mat = ctx.mat;
  vector_t vec = ctx.vec;

  // calculate how much work this thread should do and where its
  // begin/end rows are located. Leftover rows are handled by the last
  // thread.
  int rows_per_thread = mat.rows / ctx.thread_count;
  int beg_row = rows_per_thread * ctx.thread_id;
  int end_row = rows_per_thread * (ctx.thread_id+1);
  if(ctx.thread_id == ctx.thread_count-1){
    end_row = mat.rows;
  }

  // allocate memory for the thread to locally compute its results
  // avoiding the need to lock a mutex every time it adds.
  int *local_vec = malloc(sizeof(int) * vec.len);
  for(int i=0; i<vec.len; i++){
    local_vec[i] = 0;
  }

  // iterate over the matrix adding elements to the local sum for its diagonal
  for(int i=beg_row; i<end_row; i++){
    for(int j=0; j<mat.cols; j++){
      int temp = MGET(mat, i, j);
      local_vec[j - i + (mat.rows-1)] += temp;
    }
  }

  //lock mutex to update vector values
  pthread_mutex_lock(ctx.vec_lock);

  // Add on the local results to the shared vec in a loop
  for(int i = 0; i < vec.len; i++){
    VSET(vec, i, VGET(vec, i) + local_vec[i]);
  }

  pthread_mutex_unlock(ctx.vec_lock);

  // free the local vector before ending
  free(local_vec);
  return NULL;
}

int sumdiag_VER2(matrix_t mat, vector_t vec, int thread_count) {
  // OPTIMIZED CODE HERE
  return 0;
}

int sumdiag_OPTM(matrix_t mat, vector_t vec, int thread_count) {
  // call your preferred version of the function
  return sumdiag_VER1(mat, vec, thread_count);
}

////////////////////////////////////////////////////////////////////////////////
// REQUIRED: DON'T FORGET TO PASTE YOUR TIMING RESULTS FOR
// sumdiag_benchmark FROM AN ODD GRACE NODE AT THE TOP OF THIS FILE
////////////////////////////////////////////////////////////////////////////////
