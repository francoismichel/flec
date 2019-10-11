
#ifndef PICOQUIC_SEARCH_STRUCTURES_H
#define PICOQUIC_SEARCH_STRUCTURES_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <picoquic.h>
#include "../../helpers.h"

#define ROOT 1

typedef struct {
    uint64_t key;
    void *value;
} queue_item_t;


typedef struct {
    int size;
    int max_size;
    queue_item_t *buffer;
} _min_max_pq_t;

typedef _min_max_pq_t * min_max_pq_t;

static __attribute__((always_inline)) min_max_pq_t create_min_max_pq(picoquic_cnx_t *cnx, int max_size) {
    min_max_pq_t pq = my_malloc(cnx, sizeof(_min_max_pq_t));
    if (!pq)
        return NULL;
    my_memset(pq, 0, sizeof(_min_max_pq_t));
    queue_item_t *buffer = my_malloc(cnx, (max_size + 1)*sizeof(queue_item_t));
    if (!buffer) {
        my_free(cnx, pq);
    }
    my_memset(buffer, 0, (max_size + 1)*sizeof(queue_item_t));
    pq->max_size = max_size;
    pq->size = 0;
    pq->buffer = buffer;
    return pq;
}

static __attribute__((always_inline)) void delete_min_max_pq(picoquic_cnx_t *cnx, min_max_pq_t pq) {
    my_free(cnx, pq->buffer);
    my_free(cnx, pq);
}

static __attribute__((always_inline)) uint8_t log2(uint32_t val) {
    uint8_t res = 0;
    if (val > 0xFFFF) {
        res += 16;
        val >>= 16U;
    }
    if (val > 0xFF) {
        res += 8;
        val >>= 8U;
    }
    if (val > 0xF) {
        res += 4;
        val >>= 4U;
    }
    if (val > 0x3) {
        res += 2;
        val >>= 2U;
    }
    if (val > 0x1) {
        res += 1;
        val >>= 1U;
    }
    return res;
}

static __attribute__((always_inline)) bool is_in_min_level(uint32_t idx) {
    return log2(idx) % 2 == 0;
}

static __attribute__((always_inline)) bool pq_contains(min_max_pq_t pq, uint64_t key) {
    for (int i = 1 ; i <= pq->size ; i++) {
        if (pq->buffer[i].key == key)
            return true;
    }
    return false;
}

static __attribute__((always_inline)) bool pq_is_empty(min_max_pq_t pq) {
    return pq->size == 0;
}

static __attribute__((always_inline)) void swap(queue_item_t *buffer, uint32_t i, uint32_t j) {
    queue_item_t tmp = buffer[i];
    buffer[i] = buffer[j];
    buffer[j] = tmp;
}

static __attribute__((always_inline)) bool less(queue_item_t *buffer, uint32_t i, uint32_t j) {
    return buffer[i].key < buffer[j].key;
}

static __attribute__((always_inline)) bool more(queue_item_t *buffer, uint32_t i, uint32_t j) {
    return buffer[i].key > buffer[j].key;
}

static __attribute__((always_inline)) uint32_t parent(uint32_t idx) {
    return idx/2;
}

static __attribute__((always_inline)) uint32_t grand_parent(uint32_t idx) {
    return parent(parent(idx));
}

static __attribute__((always_inline)) uint32_t exists_in_heap(min_max_pq_t pq, uint32_t idx) {
    return idx != 0 && idx <= pq->size;
}

static __attribute__((always_inline)) uint32_t has_grand_parent(min_max_pq_t pq, uint32_t idx) {
    return exists_in_heap(pq, grand_parent(idx));
}


static __attribute__((always_inline)) uint32_t has_child(min_max_pq_t pq, uint32_t idx) {
    return idx*2 <= pq->size;
}

static __attribute__((always_inline)) uint32_t left_child(uint32_t idx) {
    return idx*2;
}

static __attribute__((always_inline)) uint32_t right_child(uint32_t idx) {
    return idx*2+1;
}

static __attribute__((always_inline)) bool is_root(uint32_t idx) {
    return idx == ROOT;
}



static __attribute__((always_inline)) uint32_t is_direct_child(uint32_t candidate, uint32_t parent) {
    return candidate == left_child(parent) || candidate == right_child(parent);
}

// handles the cases where a child does not exist
static __attribute__((always_inline)) uint32_t smallest_child_idx(min_max_pq_t pq, uint32_t idx) {
    uint32_t left = left_child(idx), right = right_child(idx);
    if (!exists_in_heap(pq, right))
        return left;
    // if right exists, left exists too
    if (less(pq->buffer, left, right)) {
        return left;
    }
    return right;
}

// handles the cases where a child does not exist
static __attribute__((always_inline)) uint32_t smallest_child_or_grandchild_idx(min_max_pq_t pq, uint32_t idx) {
    uint32_t smallest = left_child(idx);
    if (has_child(pq, smallest)) {
        uint32_t smallest_left_grandchild = smallest_child_idx(pq, smallest);
        smallest = less(pq->buffer, smallest, smallest_left_grandchild) ? smallest : smallest_left_grandchild;
    }
    uint32_t right = right_child(idx);
    if (exists_in_heap(pq, right)) {
        smallest = less(pq->buffer, smallest, right) ? smallest : right;
        if (has_child(pq, right)) {
            uint32_t smallest_right_grandchild = smallest_child_idx(pq, right);
            smallest = less(pq->buffer, smallest, smallest_right_grandchild) ? smallest : smallest_right_grandchild;
        }
    }
    return smallest;
}

// handles the cases where a child does not exist
static __attribute__((always_inline)) uint32_t largest_child_idx(min_max_pq_t pq, uint32_t idx) {
    uint32_t left = left_child(idx), right = right_child(idx);
    if (!exists_in_heap(pq, right))
        return left;
    // if right exists, left exists too
    if (more(pq->buffer, left, right)) {
        return left;
    }
    return right;
}

// handles the cases where a child does not exist
static __attribute__((always_inline)) uint32_t largest_child_or_grandchild_idx(min_max_pq_t pq, uint32_t idx) {
    uint32_t largest = left_child(idx);
    if (has_child(pq, largest)) {
        uint32_t largest_left_grandchild = largest_child_idx(pq, largest);
        largest = more(pq->buffer, largest, largest_left_grandchild) ? largest : largest_left_grandchild;
    }
    uint32_t right = right_child(idx);
    if (exists_in_heap(pq, right)) {
        largest = more(pq->buffer, largest, right) ? largest : right;
        if (has_child(pq, right)) {
            uint32_t largest_right_grandchild = largest_child_idx(pq, right);
            largest = more(pq->buffer, largest, largest_right_grandchild) ? largest : largest_right_grandchild;
        }
    }
    return largest;
}

static __attribute__((always_inline)) void sink_min(min_max_pq_t pq, uint32_t idx) {
    while(has_child(pq, idx)) {
        uint32_t current = idx;
        idx = smallest_child_or_grandchild_idx(pq, current);
        if (!is_direct_child(idx, current)) {
            if (less(pq->buffer, idx, current)) {
                swap(pq->buffer, idx, current);
                if (more(pq->buffer, idx, parent(idx))) {
                    swap(pq->buffer, idx, parent(idx));
                }
            }
        } else if (less(pq->buffer, idx, current)) {
            swap(pq->buffer, idx, current);
        }
    }
}

static __attribute__((always_inline)) void sink_max(min_max_pq_t pq, uint32_t idx) {
    while(has_child(pq, idx)) {
        uint32_t current = idx;
        idx = largest_child_or_grandchild_idx(pq, current);
        if (!is_direct_child(idx, current)) {
            if (more(pq->buffer, idx, current)) {
                swap(pq->buffer, idx, current);
                if (less(pq->buffer, idx, parent(idx))) {
                    swap(pq->buffer, idx, parent(idx));
                }
            }
        } else if (more(pq->buffer, idx, current)) {
            swap(pq->buffer, idx, current);
        }
    }
}


static __attribute__((always_inline)) void sink(min_max_pq_t pq, uint32_t idx) {
    if (pq->size == 1) {
        swap(pq->buffer, idx, ROOT);
        return;
    }
    if (is_in_min_level(idx)) {
        sink_min(pq, idx);
    } else {
        sink_max(pq, idx);
    }
}



static __attribute__((always_inline)) void swim_min(min_max_pq_t pq, uint32_t idx) {
    while(has_grand_parent(pq, idx) && less(pq->buffer, idx, grand_parent(idx))) {
        uint32_t gp = grand_parent(idx);
        swap(pq->buffer, idx, gp);
        idx = gp;
    }
}


static __attribute__((always_inline)) void swim_max(min_max_pq_t pq, uint32_t idx) {
    while(has_grand_parent(pq, idx) && more(pq->buffer, idx, grand_parent(idx))) {
        uint32_t gp = grand_parent(idx);
        swap(pq->buffer, idx, gp);
        idx = gp;
    }
}


static __attribute__((always_inline)) void swim(min_max_pq_t pq, uint32_t idx) {
    if (pq->size == 1) {
        swap(pq->buffer, idx, ROOT);
        return;
    }
    if (!is_root(idx)) {
        uint32_t p = parent(idx);
        if (is_in_min_level(idx)) {
            if (more(pq->buffer, idx, p)) {
                swap(pq->buffer, idx, p);
                swim_max(pq, p);
            } else {
                swim_min(pq, idx);
            }
        } else {
            if (less(pq->buffer, idx, p)) {
                swap(pq->buffer, idx, p);
                swim_min(pq, p);
            } else {
                swim_max(pq, idx);
            }
        }
    }
}

// pre: the queue must not be empty
static __attribute__((always_inline)) uint64_t pq_get_min_key(min_max_pq_t pq) {
    return pq->buffer[ROOT].key;
}

// pre: the queue must not be empty
static __attribute__((always_inline)) void pq_get_min_key_val(min_max_pq_t pq, uint64_t *key, void **val) {
    *key = pq->buffer[ROOT].key;
    *val = pq->buffer[ROOT].value;
}


static __attribute__((always_inline)) void * pq_get_min(min_max_pq_t pq) {
    if (pq->size == 0)
        return NULL;
    return pq->buffer[ROOT].value;
}

static __attribute__((always_inline)) void * pq_pop_min(min_max_pq_t pq) {
    if (pq->size == 0)
        return NULL;
    void *val = pq_get_min(pq);
    swap(pq->buffer, ROOT, pq->size--);
    sink(pq, ROOT);
    return val;
}


static __attribute__((always_inline)) void * pq_get_max(min_max_pq_t pq) {
    if (pq->size == 1)
        return pq->buffer[ROOT].value;
    // the max is the largest child of the root
    return pq->buffer[largest_child_idx(pq, ROOT)].value;
}

// pre: the queue must not be empty
static __attribute__((always_inline)) uint64_t pq_get_max_key(min_max_pq_t pq) {
    if (pq->size == 1)
        return pq->buffer[ROOT].key;
    // the max is the largest child of the root
    return pq->buffer[largest_child_idx(pq, ROOT)].key;
}

// pre: the queue must not be empty
static __attribute__((always_inline)) void pq_get_max_key_val(min_max_pq_t pq, uint64_t *key, void **val) {
    if (pq->size == 1) {
        *key = pq->buffer[ROOT].key;
        *val = pq->buffer[ROOT].value;
    }
    // the max is the largest child of the root
    *key = pq->buffer[largest_child_idx(pq, ROOT)].key;
    *val = pq->buffer[largest_child_idx(pq, ROOT)].value;
}

static __attribute__((always_inline)) void * pq_pop_max(min_max_pq_t pq) {
    if (pq->size == 0)
        return NULL;
    void *val = pq_get_max(pq);
    uint32_t max_idx = largest_child_idx(pq, ROOT);
    swap(pq->buffer, max_idx, pq->size--);
    sink(pq, max_idx);
    return val;
}


static __attribute__((always_inline)) int pq_insert(min_max_pq_t pq, uint64_t key, void *value) {
    if (pq->size == pq->max_size) {
        // pq is full
        return -1;
    }
    pq->size++;
    pq->buffer[pq->size].key = key;
    pq->buffer[pq->size].value = value;

    swim(pq, pq->size);
    return 0;
}

static __attribute__((always_inline)) void *pq_insert_and_pop_min_if_full(min_max_pq_t pq, uint64_t key, void *value) {
    void *retval = NULL;
    if (pq->size == pq->max_size) {
        // pop the minimum to make place
        retval = pq_pop_min(pq);
    }
    if (pq_insert(pq, key, value)) {
        // an error occurred
        return NULL;
    }
    return retval;
}

// pre: values has at lease size max - min
// max is excluded
// returns the number of elements that were present in the tree
// the values are inserted contiguously in the array
static __attribute__((always_inline)) int pq_get_between_bounds(min_max_pq_t pq, uint64_t min, uint64_t max, void **values) {
    queue_item_t current;
    int added = 0;
    for (int i = 1 ; i <= pq->size ; i++) {
        current = pq->buffer[i];
        // unordered version, but guarantee to have no hole in the array
        if (min <= current.key && current.key < max) {
            values[added++] = current.value;
        }
    }
    return added;
}

// pre: values has at lease size max - min
// max is excluded
// returns the number of elements that were present in the tree
// the values are inserted at index key - min in the array
static __attribute__((always_inline)) int pq_get_between_bounds_ordered(min_max_pq_t pq, uint64_t min, uint64_t max, void **values) {
    queue_item_t current;
    int added = 0;
    for (int i = 1 ; i <= pq->size ; i++) {
        current = pq->buffer[i];
        if (min <= current.key && current.key < max) {
            values[current.key - min] = current.value;
            added++;
        }
    }
    return added;
}


#endif //PICOQUIC_SEARCH_STRUCTURES_H
