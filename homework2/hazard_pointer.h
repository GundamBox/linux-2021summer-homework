#ifndef _HAZARD_POINTER_H_INCLUDED
#define _HAZARD_POINTER_H_INCLUDED

#include <assert.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <threads.h>

#include "arch.h"

#define HP_MAX_THREADS 128
#define HP_MAX_HPS 5     /* This is named 'K' in the HP paper */
#define HP_THRESHOLD_R 0 /* This is named 'R' in the HP paper */

/* Maximum number of retired objects per thread */
#define HP_MAX_RETIRED (HP_MAX_THREADS * HP_MAX_HPS)

typedef struct {
    int size;
    uintptr_t *list;
} retirelist_t;

typedef struct list_hp list_hp_t;
typedef void(list_hp_deletefunc_t)(void *);

struct list_hp {
    int max_hps;
    alignas(128) atomic_uintptr_t *hp[HP_MAX_THREADS];
    alignas(128) retirelist_t *rl[HP_MAX_THREADS * CLPAD];
    list_hp_deletefunc_t *deletefunc;
};

enum { HP_NEXT = 0, HP_CURR = 1, HP_PREV };

list_hp_t *list_hp_new(size_t max_hps, list_hp_deletefunc_t *deletefunc);
void list_hp_destroy(list_hp_t *hp);
void list_hp_clear(list_hp_t *hp);
uintptr_t list_hp_protect_ptr(list_hp_t *hp, int ihp, uintptr_t ptr);
uintptr_t list_hp_protect_release(list_hp_t *hp, int ihp, uintptr_t ptr);
void list_hp_retire(list_hp_t *hp, uintptr_t ptr);

#define TID_UNKNOWN -1

static thread_local int tid_v = TID_UNKNOWN;
static atomic_int_fast32_t tid_v_base = ATOMIC_VAR_INIT(0);
static inline int tid(void)
{
    if (tid_v == TID_UNKNOWN) {
        tid_v = atomic_fetch_add(&tid_v_base, 1);
        assert(tid_v < HP_MAX_THREADS);
    }
    return tid_v;
}

#endif