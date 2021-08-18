#include <assert.h>
#include <stdlib.h>

#include "common.h"
#include "lf_list.h"

list_node_t *list_node_new(list_key_t key)
{
    list_node_t *node = aligned_alloc(128, sizeof(*node));
    assert(node);
    *node = (list_node_t){.magic = LIST_MAGIC, .key = key};
    (void) atomic_fetch_add(&inserts, 1);
    return node;
}

void list_node_destroy(list_node_t *node)
{
    if (!node)
        return;
    assert(node->magic == LIST_MAGIC);
    free(node);
    (void) atomic_fetch_add(&deletes, 1);
}

static void __list_node_delete(void *arg)
{
    list_node_t *node = (list_node_t *) arg;
    list_node_destroy(node);
}

#ifdef USE_HARRIS_METHOD
static list_node_t *__list_find(list_t *list,
                                list_key_t key,
                                list_node_t **left_node)
{
    list_node_t *left_node_next, *right_node;
    list_node_t *curr, *curr_next;
try_again:
    do {
        curr = (list_node_t *) list->head;
        curr_next = (list_node_t *) ((list_node_t *) list->head)->next;
        do {
            if (!is_marked(curr_next)) {
                *left_node = curr;
                (void) list_hp_protect_ptr(list->hp, HP_PREV,
                                           get_unmarked(curr));
                left_node_next = curr_next;
                (void) list_hp_protect_ptr(list->hp, HP_CURR,
                                           get_unmarked(curr_next));
            }
            curr = get_unmarked_node(curr_next);
            if (curr == (list_node_t *) list->tail) {
                break;
            }
            curr_next = (list_node_t *) curr->next;
        } while (is_marked(curr_next) || curr->key < key);
        right_node = curr;
        (void) list_hp_protect_ptr(list->hp, HP_NEXT, get_unmarked(curr));

        /* 2: Check nodes are adjacent */
        if (left_node_next == right_node) {
            if ((right_node != (list_node_t *) list->tail) &&
                is_marked((right_node)->next)) {
                goto try_again; /*G1*/
            } else {
                return right_node; /*R1*/
            }
        }

        /* 3: Remove one or more marked nodes */
        if (atomic_compare_exchange_strong(&(*left_node)->next, &left_node_next,
                                           right_node)) { /*C1*/
            list_hp_retire(list->hp, get_unmarked(left_node_next));
            if (right_node != (list_node_t *) list->tail &&
                is_marked(right_node->next)) {
                goto try_again; /*G2*/
            } else {
                return right_node; /*R2*/
            }
        }
    } while (true); /*B2*/
}

bool list_insert(list_t *list, list_key_t key)
{
    list_node_t *new_node = list_node_new(key);
    list_node_t *next, *prev;

    do {
        next = __list_find(list, key, &prev);
        if (next != (list_node_t *) list->tail && next->key == key) { /*T1*/
            list_node_destroy(new_node);
            list_hp_clear(list->hp);
            return false;
        }
        new_node->next = (uintptr_t) next;
        if (atomic_compare_exchange_strong(&prev->next, &next,
                                           new_node)) { /*C2*/
            list_hp_clear(list->hp);
            return true;
        }
    } while (true); /*B3*/
}

bool list_delete(list_t *list, list_key_t key)
{
    list_node_t *curr = NULL, *next = NULL, *prev = NULL;
    do {
        curr = __list_find(list, key, &prev);
        if (curr == (list_node_t *) list->tail || curr->key != key) { /*T1*/
            list_hp_clear(list->hp);
            return false;
        }
        next = (list_node_t *) curr->next;
        if (!is_marked(next)) {
            if (atomic_compare_exchange_strong((list_node_t **) &(curr->next),
                                               &next,
                                               get_marked(next))) { /*C3*/
                break;
            }
        }
    } while (true); /*B4*/

    if (atomic_compare_exchange_strong((list_node_t **) &(prev->next), &curr,
                                       next)) { /*C4*/
        // curr = __list_find(list, curr->key, &prev);
        list_hp_clear(list->hp);
        list_hp_retire(list->hp, get_unmarked(curr));
    } else {
        list_hp_clear(list->hp);
    }

    return true;
}
#else
static bool __list_find(list_t *list,
                        list_key_t *key,
                        list_node_t **par_prev,
                        list_node_t **par_curr,
                        list_node_t **par_next)
{
    list_node_t *prev = NULL, *curr = NULL, *next = NULL;

try_again:
    prev = list->head;
    curr = (list_node_t *) atomic_load(&(prev->next));
    (void) list_hp_protect_ptr(list->hp, HP_CURR, (uintptr_t) curr);
    if (atomic_load(&(prev->next)) != get_unmarked(curr))
        goto try_again;
    while (true) {
        next = (list_node_t *) atomic_load(&get_unmarked_node(curr)->next);
        (void) list_hp_protect_ptr(list->hp, HP_NEXT, get_unmarked(next));
        // curr 到 next 中間被插隊，重試
        if (atomic_load(&get_unmarked_node(curr)->next) != (uintptr_t) next)
            goto try_again;
        // prev 到 curr 中間被插隊，重試
        if (atomic_load(&get_unmarked_node(prev)->next) != get_unmarked(curr))
            goto try_again;
        // 到 tail 還找不到
        if (get_unmarked(curr) == atomic_load((atomic_uintptr_t *) &list->tail))
            break;
        if (get_unmarked_node(curr) == curr) {
            if (!(get_unmarked_node(curr)->key < *key)) {
                // curr 沒被 mark 且 curr->key >= key
                *par_prev = prev;
                *par_curr = curr;
                *par_next = next;
                return get_unmarked_node(curr)->key == *key;
            }
            prev = get_unmarked_node(curr);
            (void) list_hp_protect_release(list->hp, HP_PREV,
                                           get_unmarked(curr));
        } else {
            uintptr_t tmp = get_unmarked(curr);
            if (!atomic_compare_exchange_strong(&(prev->next), &tmp,
                                                get_unmarked(next)))
                goto try_again;
            list_hp_retire(list->hp, get_unmarked(curr));
        }
        curr = next;
        (void) list_hp_protect_release(list->hp, HP_CURR, get_unmarked(next));
    }
    *par_prev = prev;
    *par_curr = curr;
    *par_next = next;

    return false;
}

bool list_insert(list_t *list, list_key_t key)
{
    list_node_t *prev = NULL, *curr = NULL, *next = NULL;

    list_node_t *node = list_node_new(key);

    while (true) {
        if (__list_find(list, &key, &prev, &curr, &next)) {
            list_node_destroy(node);
            list_hp_clear(list->hp);
            return false;
        }
        atomic_store_explicit(&node->next, (uintptr_t) curr,
                              memory_order_relaxed);
        uintptr_t tmp = get_unmarked(prev->next);
        if (atomic_compare_exchange_strong(&(prev->next), &tmp,
                                           (uintptr_t) node)) {
            list_hp_clear(list->hp);
            return true;
        }
    }
}

bool list_delete(list_t *list, list_key_t key)
{
    list_node_t *prev, *curr, *next;

    while (true) {
        if (!__list_find(list, &key, &prev, &curr, &next)) {
            list_hp_clear(list->hp);
            return false;
        }

        uintptr_t tmp = get_unmarked(curr);

        if (!atomic_compare_exchange_strong(&curr, &tmp, get_marked(curr))) {
            continue;
        }

        tmp = get_unmarked(curr);
        if (atomic_compare_exchange_strong(&(prev->next), &tmp,
                                           get_unmarked(next))) {
            list_hp_clear(list->hp);
            list_hp_retire(list->hp, get_unmarked(curr));
        } else {
            list_hp_clear(list->hp);
        }
        return true;
    }
}
#endif

list_t *list_new(void)
{
    list_t *list = calloc(1, sizeof(*list));
    assert(list);
    list_node_t *head = list_node_new(0), *tail = list_node_new(UINTPTR_MAX);
    assert(head), assert(tail);
    list_hp_t *hp = list_hp_new(3, __list_node_delete);

    atomic_init(&head->next, (uintptr_t) tail);
    *list = (list_t){.hp = hp};
    atomic_init(&list->head, (uintptr_t) head);
    atomic_init(&list->tail, (uintptr_t) tail);

    return list;
}

void list_destroy(list_t *list)
{
    assert(list);
    list_node_t *prev = (list_node_t *) atomic_load(&list->head);
    list_node_t *node = (list_node_t *) atomic_load(&prev->next);
    while (node) {
        list_node_destroy(prev);
        prev = node;
        node = (list_node_t *) atomic_load(&prev->next);
    }
    list_node_destroy(prev);
    list_hp_destroy(list->hp);
    free(list);
}

void list_print(list_t *list)
{
    DEBUG_PRINT("debug info: ");
    list_node_t *curr = NULL, *next = NULL;
    curr = get_unmarked_node(list->head);
    for (; curr; curr = get_unmarked_node(curr->next)) {
        if (get_unmarked_node(curr->next)) {
            DEBUG_PRINT("0x%012lx -> ", curr->key);
        } else {
            DEBUG_PRINT("0x%012lx", curr->key);
        }
        next = get_unmarked_node(curr->next);
        if (next) {
            assert(curr->key < next->key);
        }
    }
    DEBUG_PRINT("\n");
}