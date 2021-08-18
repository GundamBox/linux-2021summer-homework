/*
 * Hazard pointers are a mechanism for protecting objects in memory from
 * being deleted by other threads while in use. This allows safe lock-free
 * data structures.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "arch.h"
#include "hazard_pointer.h"
#include "lf_list.h"

#define N_ELEMENTS 128
#define N_THREADS (64)
#define MAX_THREADS 128

static uintptr_t elements[MAX_THREADS + 1][N_ELEMENTS];

static void *insert_thread(void *arg)
{
    list_t *list = (list_t *) arg;

    for (size_t i = 0; i < N_ELEMENTS; i++) {
        (void) list_insert(list, (uintptr_t) &elements[tid()][i]);
        list_print(list);
    }
    return NULL;
}

static void *delete_thread(void *arg)
{
    list_t *list = (list_t *) arg;

    for (size_t i = 0; i < N_ELEMENTS; i++) {
        (void) list_delete(list, (uintptr_t) &elements[tid()][i]);
        list_print(list);
    }
    return NULL;
}

#include <time.h>
#define BILLION 1E9

int main(void)
{
    struct timespec start, stop;

    clock_gettime(CLOCK_REALTIME, &start);

    list_t *list = list_new();
    pthread_t thr[N_THREADS];
    for (size_t i = 0; i < N_THREADS; i++)
        pthread_create(&thr[i], NULL, (i & 1) ? delete_thread : insert_thread,
                       list);

    for (size_t i = 0; i < N_THREADS; i++)
        pthread_join(thr[i], NULL);

    for (size_t i = 0; i < N_ELEMENTS; i++) {
        for (size_t j = 0; j < tid_v_base; j++)
            list_delete(list, (uintptr_t) &elements[j][i]);
    }
    list_destroy(list);

    clock_gettime(CLOCK_REALTIME, &stop);

    double accum =
        (stop.tv_sec - start.tv_sec) + (stop.tv_nsec - start.tv_nsec) / BILLION;

    fprintf(stderr, "accum_time = %lf\n", accum);
    fprintf(stderr, "inserts = %zu, deletes = %zu\n", atomic_load(&inserts),
            atomic_load(&deletes));

    return 0;
}