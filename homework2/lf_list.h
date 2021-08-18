#ifndef _LF_LIST_H_INCLUDED
#define _LF_LIST_H_INCLUDED

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hazard_pointer.h"

#define is_marked(p) (bool) ((uintptr_t)(p) &0x01)
#define get_marked(p) ((uintptr_t)(p) | (0x01))
#define get_unmarked(p) ((uintptr_t)(p) & (~0x01))

#define get_marked_node(p) ((list_node_t *) get_marked(p))
#define get_unmarked_node(p) ((list_node_t *) get_unmarked(p))

typedef uintptr_t list_key_t;

typedef struct list_node {
    alignas(128) uint32_t magic;
    alignas(128) atomic_uintptr_t next;
    list_key_t key;
} list_node_t;

/* Per list variables */

typedef struct list {
    atomic_uintptr_t head, tail;
    list_hp_t *hp;
} list_t;

#define LIST_MAGIC (0xDEADBEAF)

list_node_t *list_node_new(list_key_t key);
void list_node_destroy(list_node_t *node);
static void __list_node_delete(void *arg);
#ifdef USE_HARRIS_METHOD
static list_node_t *__list_find(list_t *list,
                                list_key_t key,
                                list_node_t **left_node);
bool list_insert(list_t *list, list_key_t key);
bool list_delete(list_t *list, list_key_t key);
#else
static bool __list_find(list_t *list,
                        list_key_t *key,
                        list_node_t **par_prev,
                        list_node_t **par_curr,
                        list_node_t **par_next);
bool list_insert(list_t *list, list_key_t key);
bool list_delete(list_t *list, list_key_t key);
#endif

list_t *list_new(void);
void list_destroy(list_t *list);
void list_print(list_t *list);

static atomic_uint_fast32_t deletes = 0, inserts = 0;

#endif