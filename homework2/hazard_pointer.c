#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "hazard_pointer.h"

/* Create a new hazard pointer array of size 'max_hps' (or a reasonable
 * default value if 'max_hps' is 0). The function 'deletefunc' will be
 * used to delete objects protected by hazard pointers when it becomes
 * safe to retire them.
 */
list_hp_t *list_hp_new(size_t max_hps, list_hp_deletefunc_t *deletefunc)
{
    list_hp_t *hp = aligned_alloc(128, sizeof(*hp));
    assert(hp);

    if (max_hps == 0)
        max_hps = HP_MAX_HPS;

    *hp = (list_hp_t){.max_hps = max_hps, .deletefunc = deletefunc};

    for (int i = 0; i < HP_MAX_THREADS; i++) {
        hp->hp[i] = calloc(CLPAD * 2, sizeof(hp->hp[i][0]));
        hp->rl[i * CLPAD] = calloc(1, sizeof(*hp->rl[0]));
        for (int j = 0; j < hp->max_hps; j++)
            atomic_init(&hp->hp[i][j], 0);
        hp->rl[i * CLPAD]->list = calloc(HP_MAX_RETIRED, sizeof(uintptr_t));
    }

    return hp;
}

/* Destroy a hazard pointer array and clean up all objects protected
 * by hazard pointers.
 */
void list_hp_destroy(list_hp_t *hp)
{
    for (int i = 0; i < HP_MAX_THREADS; i++) {
        free(hp->hp[i]);
        retirelist_t *rl = hp->rl[i * CLPAD];
        for (int j = 0; j < rl->size; j++) {
            void *data = (void *) rl->list[j];
            hp->deletefunc(data);
        }
        free(rl->list);
        free(rl);
    }
    free(hp);
}

/* Clear all hazard pointers in the array for the current thread.
 * Progress condition: wait-free bounded (by max_hps)
 */
void list_hp_clear(list_hp_t *hp)
{
    for (int i = 0; i < hp->max_hps; i++)
        atomic_store_explicit(&hp->hp[tid()][i], 0, memory_order_release);
}

/* This returns the same value that is passed as ptr.
 * Progress condition: wait-free population oblivious.
 */
uintptr_t list_hp_protect_ptr(list_hp_t *hp, int ihp, uintptr_t ptr)
{
    atomic_store(&hp->hp[tid()][ihp], ptr);
    return ptr;
}

/* Same as list_hp_protect_ptr(), but explicitly uses memory_order_release.
 * Progress condition: wait-free population oblivious.
 */
uintptr_t list_hp_protect_release(list_hp_t *hp, int ihp, uintptr_t ptr)
{
    atomic_store_explicit(&hp->hp[tid()][ihp], ptr, memory_order_release);
    return ptr;
}

/* Retire an object that is no longer in use by any thread, calling
 * the delete function that was specified in list_hp_new().
 *
 * Progress condition: wait-free bounded (by the number of threads squared)
 */
void list_hp_retire(list_hp_t *hp, uintptr_t ptr)
{
    retirelist_t *rl = hp->rl[tid() * CLPAD];
    rl->list[rl->size++] = ptr;
    assert(rl->size < HP_MAX_RETIRED);

    if (rl->size < HP_THRESHOLD_R)
        return;

    for (size_t iret = 0; iret < rl->size; iret++) {
        uintptr_t obj = rl->list[iret];
        bool can_delete = true;
        for (int itid = 0; itid < HP_MAX_THREADS && can_delete; itid++) {
            for (int ihp = hp->max_hps - 1; ihp >= 0; ihp--) {
                if (atomic_load(&hp->hp[itid][ihp]) == obj) {
                    can_delete = false;
                    break;
                }
            }
        }

        if (can_delete) {
            size_t bytes = (rl->size - iret) * sizeof(rl->list[0]);
            memmove(&rl->list[iret], &rl->list[iret + 1], bytes);
            rl->size--;
            hp->deletefunc((void *) obj);
        }
    }
}