
#ifndef ONLINE_GAUSSIAN_ARRAYLIST_H
#define ONLINE_GAUSSIAN_ARRAYLIST_H
#include "../headers/symbol.h"
#include "../headers/util.h"
typedef struct {
    uintmax_t *array;
    int max_size;
    int current_size;
} arraylist_t;


static __attribute__((always_inline)) int inline arraylist_is_empty(arraylist_t *arraylist) {
    return arraylist->current_size == 0;
}

static __attribute__((always_inline)) int inline arraylist_size(arraylist_t *arraylist) {
    return arraylist->current_size;
}


static __attribute__((always_inline)) int arraylist_init(picoquic_cnx_t *cnx, arraylist_t *arraylist, int initial_size) {
    arraylist->max_size = initial_size;
    arraylist->current_size = 0;
    arraylist->array = malloc_fn(cnx, initial_size * sizeof(uintmax_t));
    return 0;
}
static __attribute__((always_inline)) int arraylist_reset(arraylist_t *arraylist) {
    arraylist->current_size = 0;
    return 0;
}

static __attribute__((always_inline)) int arraylist_push(picoquic_cnx_t *cnx, arraylist_t *arraylist, uintmax_t id) {
    if (arraylist->current_size == arraylist->max_size) {
        arraylist->array = realloc_fn(cnx, arraylist->array, 2 * arraylist->max_size*sizeof(uintmax_t));
        if (!arraylist->array) {
            return -1;
        }
        arraylist->max_size *= 2;
    }
    arraylist->array[arraylist->current_size++] = id;
    return 0;
}

// shifts the arraylist of n elements to the left, removing the n first elements of the arraylist
static __attribute__((always_inline)) int arraylist_shift_left(picoquic_cnx_t *cnx, arraylist_t *arraylist, int n) {
    if (n == 0) {
        return 0;
    }
    if (n > arraylist->current_size) {
        // not possible
        return -1;
    }
    if (n == arraylist->current_size) {
        arraylist->current_size = 0;
        return 0;
    }
    memmove_fn(arraylist->array, &arraylist->array[n], (arraylist->current_size - n)*sizeof(uintmax_t));
    arraylist->current_size -= n;
    return 0;
}


// finds the index of the first occurence of val in the arraylist, returns -1 if not present
static __attribute__((always_inline)) int arraylist_index(arraylist_t *arraylist, uintmax_t val) {
    for (int i = 0 ; i < arraylist->current_size ; i++) {
        if (arraylist->array[i] == val) {
            return i;
        }
    }
    return -1;
}

static __attribute__((always_inline)) uintmax_t arraylist_get(arraylist_t *arraylist, int index) {
    if (index < 0 || index >= arraylist->current_size) {
        return 0;
    }
    return arraylist->array[index];
}

// pre: must not be empty
static __attribute__((always_inline)) uintmax_t arraylist_get_first(arraylist_t *arraylist) {
    if (arraylist->current_size == 0) {
        return 0;
    }
    return arraylist_get(arraylist, 0);
}
// pre: must not be empty
static __attribute__((always_inline)) uintmax_t arraylist_get_last(arraylist_t *arraylist) {
    if (arraylist->current_size == 0) {
        return 0;
    }
    return arraylist_get(arraylist, arraylist->current_size - 1);
}

static __attribute__((always_inline)) int arraylist_set(arraylist_t *arraylist, int index, uintmax_t val) {
    if (index < 0 || index >= arraylist->current_size) {
        return -1;
    }
    arraylist->array[index] = val;
    return 0;
}

// pre: should not be empty !
static __attribute__((always_inline)) uintmax_t arraylist_pop(arraylist_t *arraylist) {
    if(arraylist->current_size == 0) return 0;
    return arraylist->array[--arraylist->current_size];
}


#endif //ONLINE_GAUSSIAN_ARRAYLIST_H
