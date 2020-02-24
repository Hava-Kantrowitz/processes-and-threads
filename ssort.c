//NOTE: THIS CODE IS BASED OFF WHAT WE DISCUSSED IN CLASS. ATTRIBUTION, NAT TUCK, CLASS. 
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <semaphore.h>

#include "float_vec.h"
#include "barrier.h"
#include "utils.h"

int
comparing(const void* first, const void* second) {
	float one = *((float*)first);
	float two = *((float*)second);
	return (one > two) - (one < two); 
}

void
qsort_floats(floats* xs)
{
    // call qsort to sort the array
	qsort(xs->data, xs->size, sizeof(float), comparing); 
}

floats*
sample(float* data, long size, int P)
{
    // sample the input data, per the algorithm description
    int numSort = 3 * (P-1);

    srand(time(0));

    floats* randomFloats = make_floats(numSort);
    for (int i = 0; i < numSort; i++) {
	int randyNum = rand() % size; 
	floats_push(randomFloats, *(data + randyNum)); 
    }

    qsort_floats(randomFloats);

    floats* sampleFloats = make_floats(P-1);
    for (int i = 0; i < (P-1); i++) {
	    float firstFloat = randomFloats->data[(i*3)];
	    float secondFloat = randomFloats->data[(i*3)+1];
	    float thirdFloat = randomFloats->data[(i*3)+2];
	    float median = (firstFloat + secondFloat + thirdFloat) / 3;
	    floats_push(sampleFloats, median);
    } 

    floats* finalFloats = make_floats(P-1);
    floats_push(finalFloats, 0);
    for (int i = 0; i < sampleFloats->size; i++) {
	   floats_push(finalFloats, sampleFloats->data[i]);
    }

    floats_push(finalFloats, FLT_MAX);
    free_floats(randomFloats);
    free_floats(sampleFloats); 

    //floats_print(finalFloats);  
 
    return finalFloats;
}

void
sort_worker(int pnum, float* data, long size, int P, floats* samps, long* sizes, barrier* bb)
{
    floats* xs = make_floats(10);
    // select the floats to be sorted by this worker
    for (long i = 0; i < size; i++) {
	float nextNum = data[i];
	if (nextNum >= samps->data[pnum] && nextNum < samps->data[pnum+1]) {
		floats_push(xs, nextNum);
	}
    }

    printf("%d: start %.04f, count %ld\n", pnum, samps->data[pnum], xs->size);

    //write number of items to shared array at pnum
    sizes[pnum] = xs->size;
    //printf("Size p is %li\n", sizes[pnum]); 

    qsort_floats(xs);
    //printf("Sorted floats is ");
    //floats_print(xs); 

    //copy local arrays -- need to avoid data race here
    barrier_wait(bb); 
    
    int start = 0;
    for (int i = 0; i <= pnum-1; i++) {
	start += sizes[i];
    }

    int end = 0;
    for (int i = 0; i <= pnum; i++) {
	    end += sizes[i];
    }
    end--; 

    //printf("Start value is %d for process %d\n", start, pnum);
    //printf("End value is %d for process %d\n", end, pnum); 

    //barrier_wait(bb);
    int j = 0;
    for (int i = start; i <= end; i++) {
	data[i] = xs->data[j];
	j++;
    }

    free_floats(xs);
    exit(0); 
}

void
run_sort_workers(float* data, long size, int P, floats* samps, long* sizes, barrier* bb)
{
    pid_t kids[P];
    (void) kids; // suppress unused warning

    // spawn P processes, each running sort_worker
    for (int i = 0; i < P; i++) {
	    if ((kids[i] = fork())){
		    //we don't do anything here
	    }
	    else {
		    sort_worker(i, data, size, P, samps, sizes, bb); 
	    }
    }

    for (int ii = 0; ii < P; ++ii) {
        int rv = waitpid(kids[ii], 0, 0);
        check_rv(rv);
    }

}

void
sample_sort(float* data, long size, int P, long* sizes, barrier* bb)
{
    floats* samps = sample(data, size, P);
    run_sort_workers(data, size, P, samps, sizes, bb);
    free_floats(samps);
}

int
main(int argc, char* argv[])
{
    alarm(120);

    if (argc != 3) {
        printf("Usage:\n");
        printf("\t%s P data.dat\n", argv[0]);
        return 1;
    }

    const int P = atoi(argv[1]);
    const char* fname = argv[2];

    seed_rng();

    int rv;
    struct stat st;
    rv = stat(fname, &st);
    check_rv(rv);

    const int fsize = st.st_size;
    if (fsize < 8) {
        printf("File too small.\n");
        return 1;
    }

    int fd = open(fname, O_RDWR);
    check_rv(fd);

    long* file = mmap(0, fsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    long count = file[0];
    float* data = ((float*)(file + 1));

    /*for (int i = 0; i < count; i++) {
	    printf("Data at %d is %f\n", i, *(data + (1*i)));
    }*/

    long sizes_bytes = P * sizeof(long);
    long* sizes = mmap(0, sizes_bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0); //This should be shared

    barrier* bb = make_barrier(P);

    sample_sort(data, count, P, sizes, bb);
    /*for (int i = 0; i < count; i++) {
	    printf("Data[%d] is %f\n", i, data[i]);
    }*/

    free_barrier(bb);

    //munmap your mmaps
    munmap(sizes, sizes_bytes); 
    munmap(file, fsize); 

    return 0;
}

