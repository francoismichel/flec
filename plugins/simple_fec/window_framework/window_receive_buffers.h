#ifndef PICOQUIC_WINDOW_RECEIVE_BUFFERS_H
#define PICOQUIC_WINDOW_RECEIVE_BUFFERS_H

#include <red_black_tree.h>
#include "../fec.h"
#include "types.h"
#include "search_structures.h"

typedef struct {
    min_max_pq_t pq;
} received_source_symbols_buffer_t;

typedef struct {
    int max_size;
    red_black_tree_t tree;
} tree_based_received_source_symbols_buffer_t;

typedef struct {
    uint32_t max_size;
    uint32_t current_size;
    uint32_t first_index;
    window_source_symbol_id_t first_id;
    window_source_symbol_t **array;
} ring_based_received_source_symbols_buffer_t;

static __attribute__((always_inline)) received_source_symbols_buffer_t *new_source_symbols_buffer(picoquic_cnx_t *cnx,
                                                                                                  int max_size) {
    received_source_symbols_buffer_t *buffer = my_malloc(cnx, sizeof(received_source_symbols_buffer_t));
    if (!buffer)
        return NULL;
    buffer->pq = create_min_max_pq(cnx, max_size);
    if (!buffer->pq) {
        my_free(cnx, buffer);
        return NULL;
    }
    return buffer;
}


static __attribute__((always_inline)) ring_based_received_source_symbols_buffer_t *new_ring_based_source_symbols_buffer(picoquic_cnx_t *cnx,
                                                                                                  int max_size) {
    if (max_size == 0) {
        return NULL;
    }
    ring_based_received_source_symbols_buffer_t *buffer = my_malloc(cnx, sizeof(ring_based_received_source_symbols_buffer_t));
    if (!buffer)
        return NULL;
    buffer->array = my_malloc(cnx, max_size*sizeof(source_symbol_id_t *));
    if (!buffer->array) {
        my_free(cnx, buffer);
        return NULL;
    }
    buffer->max_size = max_size;
    buffer->current_size = 0;
    buffer->first_id = 0;
    buffer->first_index = 0;
    return buffer;
}

static __attribute__((always_inline)) tree_based_received_source_symbols_buffer_t *new_tree_based_source_symbols_buffer(picoquic_cnx_t *cnx,
                                                                                                  int max_size) {
    tree_based_received_source_symbols_buffer_t *buffer = my_malloc(cnx, sizeof(tree_based_received_source_symbols_buffer_t));
    if (!buffer)
        return NULL;
    buffer->max_size = max_size;
    rbt_init(cnx, &buffer->tree);
    return buffer;
}

static __attribute__((always_inline)) void release_source_symbols_buffer(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer) {
    delete_min_max_pq(cnx, buffer->pq);
    my_free(cnx, buffer);
}


static __attribute__((always_inline)) void release_ring_based_source_symbols_buffer(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer) {
    my_free(cnx, buffer->array);
    my_free(cnx, buffer);
}

static __attribute__((always_inline)) void release_tree_based_source_symbols_buffer(picoquic_cnx_t *cnx, tree_based_received_source_symbols_buffer_t *buffer) {
    while(!rbt_is_empty(cnx, &buffer->tree)) {
        rbt_delete_min(cnx, &buffer->tree);
    }
    my_free(cnx, buffer);
}

// returns the symbol that has been removed if the buffer was full
static __attribute__((always_inline)) window_source_symbol_t *add_source_symbol(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer, window_source_symbol_t *ss) {
    return pq_insert_and_pop_min_if_full(buffer->pq, ss->id, ss);
}

// returns the symbol that has been removed if the buffer was full
static __attribute__((always_inline)) window_source_symbol_t *tree_based_source_symbols_buffer_add_source_symbol(picoquic_cnx_t *cnx, tree_based_received_source_symbols_buffer_t *buffer, window_source_symbol_t *ss) {
//    return pq_insert_and_pop_min_if_full(buffer->pq, ss->id, ss);
    rbt_key removed_id = 0;
    rbt_val removed = 0;
    window_source_symbol_t *removed_ss = NULL;

    if (rbt_size(cnx, &buffer->tree) == buffer->max_size) {
        rbt_delete_and_get_min(cnx, &buffer->tree, &removed_id, &removed);
        removed_ss = (window_source_symbol_t *) removed;
    }
    rbt_put(cnx, &buffer->tree, ss->id, ss);
    return removed_ss;
}

static __attribute__((always_inline)) void ring_based_source_symbols_buffer_remove_and_free_first(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer) {
    if (buffer->current_size > 0) {
        window_source_symbol_t *old_ss = buffer->array[buffer->first_index];
        PROTOOP_PRINTF(cnx, "REMOVE INDEX %u = %p, array[802] = %p \n", buffer->first_index, (protoop_arg_t) old_ss, (protoop_arg_t) buffer->array[802]);
        if (old_ss) {
            PROTOOP_PRINTF(cnx, "BEFORE DELETE\n");
            delete_window_source_symbol(cnx, old_ss);
            PROTOOP_PRINTF(cnx, "AFTER DELETE\n");
        }
        buffer->array[buffer->first_index] = NULL;
        buffer->first_id++;
        buffer->first_index = (buffer->first_index + 1) % buffer->max_size;
        buffer->current_size--;
    }
    if (buffer->current_size == 0) {
        buffer->first_id = 0;
        buffer->first_index = 0;
    }
}



// pre: the buffer should not be empty
static __attribute__((always_inline)) source_symbol_id_t get_first_source_symbol_id(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer) {
    return ((window_source_symbol_t *) pq_get_min(buffer->pq))->id;
}


// pre: the buffer should not be empty
static __attribute__((always_inline)) source_symbol_id_t ring_based_source_symbols_buffer_get_first_source_symbol_id(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer) {
    return buffer->first_id;
}

// pre: the buffer should not be empty
static __attribute__((always_inline)) source_symbol_id_t tree_based_source_symbols_buffer_get_first_source_symbol_id(picoquic_cnx_t *cnx, tree_based_received_source_symbols_buffer_t *buffer) {
//    return ((window_source_symbol_t *) pq_get_min(buffer->pq))->id;
    return (source_symbol_id_t) rbt_min_key(cnx, &buffer->tree);
}

// pre: the buffer should not be empty
static __attribute__((always_inline)) source_symbol_id_t get_last_source_symbol_id(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer) {
    return ((window_source_symbol_t *) pq_get_max(buffer->pq))->id;
}

// pre: the buffer should not be empty
static __attribute__((always_inline)) source_symbol_id_t tree_based_source_symbols_buffer_get_last_source_symbol_id(picoquic_cnx_t *cnx, tree_based_received_source_symbols_buffer_t *buffer) {
    return (source_symbol_id_t) rbt_max_key(cnx, &buffer->tree);
}

// pre: the buffer should not be empty
static __attribute__((always_inline)) source_symbol_id_t ring_based_source_symbols_buffer_get_last_source_symbol_id(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer) {
    return buffer->first_id + (buffer->current_size - 1);
}

static __attribute__((always_inline)) uint64_t tree_based_source_symbols_buffer_contains_id(picoquic_cnx_t *cnx, tree_based_received_source_symbols_buffer_t *buffer, source_symbol_id_t id) {
    return rbt_contains(cnx, &buffer->tree, id);
}

static __attribute__((always_inline)) window_source_symbol_t  *tree_based_source_symbols_buffer_get(picoquic_cnx_t *cnx, tree_based_received_source_symbols_buffer_t *buffer, source_symbol_id_t id) {
    rbt_val val = NULL;
    rbt_get(cnx, &buffer->tree, id, &val);
    return val;
}


static __attribute__((always_inline)) window_source_symbol_t  *ring_based_source_symbols_buffer_get(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer, source_symbol_id_t id) {
    if (id < buffer->first_id || id > ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer)) {
        return NULL;
    }
    return buffer->array[(id - buffer->first_id + buffer->first_index) % buffer->max_size];
}

static __attribute__((always_inline)) void _ring_based_source_symbols_buffer_remove_and_free(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer, uint32_t index) {
    if (buffer->array[index] != NULL) {
        delete_window_source_symbol(cnx, buffer->array[index]);
        buffer->array[index] = NULL;
    }

}


static __attribute__((always_inline)) int ring_based_source_symbols_buffer_add_source_symbol(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer, window_source_symbol_t *ss) {

    PROTOOP_PRINTF(cnx, "ADD SYMBOL %u, CURRENT SIZE = %u, MAX SIZE = %u, FIRST ID = %u, LAST ID %u, FIRST INDEX = %u\n", ss->id, buffer->current_size,
            buffer->max_size, buffer->first_id, ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer), buffer->first_index);
    if (buffer->current_size > 0 && ss->id > ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer)) {
        uint32_t added_symbols = ss->id - ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer);
        if (added_symbols > buffer->max_size) {
            // corner case: in this case, we consider that the added ID is the last of the buffer and all the others (lower IDs) are NULL
            for (uint32_t i = 0 ; i < buffer->max_size ; i++) {
                _ring_based_source_symbols_buffer_remove_and_free(cnx, buffer, i);
            }
//            my_memset(buffer->array, 0, buffer->max_size*sizeof(window_source_symbol_t *));
            buffer->first_index = 0;
            buffer->first_id = ss->id - (buffer->max_size - 1);
            return 0;
        }
        while (buffer->current_size + added_symbols > buffer->max_size) {
            PROTOOP_PRINTF(cnx, "REMOVE FIRST\n");
            ring_based_source_symbols_buffer_remove_and_free_first(cnx, buffer);
            PROTOOP_PRINTF(cnx, "AFTER REMOVE FIRST, array[802] = %p\n", (protoop_arg_t) buffer->array[802]);
        }
        for (window_source_symbol_id_t id = 1 + ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer) ; id < ss->id ; id++) {
            _ring_based_source_symbols_buffer_remove_and_free(cnx, buffer, (id - buffer->first_id + buffer->first_index) % buffer->max_size);
        }
        PROTOOP_PRINTF(cnx, "AFTER LOOP AND BEFORE SET, array[802] = %p\n", (protoop_arg_t) buffer->array[802]);
        buffer->array[(ss->id - buffer->first_id + buffer->first_index) % buffer->max_size] = ss;
        PROTOOP_PRINTF(cnx, "PUT %u AT INDEX %u (FIRST INDEX = %u), array[802] = %p\n", ss->id, (ss->id - buffer->first_id + buffer->first_index) % buffer->max_size, buffer->first_index, (protoop_arg_t) buffer->array[802]);
        buffer->current_size += (ss->id - ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer));
        return 0;
    }
    if (buffer->current_size == 0) {
        // here we really want to avoid adding new source symbols smaller than first_id so we add all the missing symbols
        // received before the one that we need to add
        my_memset(buffer->array, 0, buffer->max_size*sizeof(window_source_symbol_t *));
        buffer->first_index = 0;
        buffer->first_id = WINDOW_INITIAL_SYMBOL_ID;
        if (ss->id >= buffer->max_size) {
            buffer->first_id = MAX(buffer->first_id, ss->id - (buffer->max_size - 1));
        }
        buffer->current_size = MIN(buffer->max_size, (ss->id + 1 - buffer->first_id));
        buffer->array[buffer->current_size - 1] = ss;
        return 0;
    }
    if (ss->id < buffer->first_id) {
        PROTOOP_PRINTF(cnx, "SYMBOL TO ADD HAS AN ID LOWER THAN THE BUFFER'S FIRST ID\n");
        return 0;
    }

    window_source_symbol_id_t idx = (ss->id - buffer->first_id + buffer->first_index) % buffer->max_size;
    if (buffer->array[idx] != NULL) {
        PROTOOP_PRINTF(cnx, "ERROR: OVERWRITING ALREADY PRESENT SOURCE SYMBOM IN BUFFER\n");
    }
    PROTOOP_PRINTF(cnx, "PUT SYMBOL %p AT ID %d\n", (protoop_arg_t) ss, idx);
    buffer->array[idx] = ss;
    return 0;
}

static __attribute__((always_inline)) int ring_based_source_symbols_buffer_contains(picoquic_cnx_t *cnx, ring_based_received_source_symbols_buffer_t *buffer, window_source_symbol_id_t id) {

    if (id < buffer->first_id || id > ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, buffer)) {
        return false;
    }
    window_source_symbol_t *ss = buffer->array[(id - buffer->first_id + buffer->first_index) % buffer->max_size];
    return ss != NULL && ss->id == id;
}

// inserts the source symbols in the provided buffer starting with the symbol with the smallest sfpid set at the first entry of the array and does not change an entry where source symbol is missing
// returns the number of received source symbols (total - missing ones)
static __attribute__((always_inline)) int get_source_symbols_between_bounds(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer, window_source_symbol_t **symbols, uint32_t min_symbol, uint32_t max_symbol) {
    my_memset(symbols, 0, max_symbol + 1 - min_symbol);
    return pq_get_between_bounds_ordered(buffer->pq, min_symbol, max_symbol + 1, (void **) symbols);
}

typedef struct {
    min_max_pq_t pq;
} received_repair_symbols_buffer_t;



static __attribute__((always_inline)) received_repair_symbols_buffer_t *new_repair_symbols_buffer(picoquic_cnx_t *cnx,
                                                                                                  int max_size) {
    received_repair_symbols_buffer_t *buffer = my_malloc(cnx, sizeof(received_source_symbols_buffer_t));
    if (!buffer)
        return NULL;
    buffer->pq = create_min_max_pq(cnx, max_size);
    if (!buffer->pq) {
        my_free(cnx, buffer);
        return NULL;
    }
    return buffer;
}

static __attribute__((always_inline)) void release_repair_symbols_buffer(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer) {
    delete_min_max_pq(cnx, buffer->pq);
    my_free(cnx, buffer);
}

// returns the symbol that has been removed if the buffer was full
static __attribute__((always_inline)) repair_symbol_t *add_repair_symbol(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer, window_repair_symbol_t *rs) {
    // FIXME: do a simple ring buffer (is it still needed ?)
    // we order it by the last protected symbol
    PROTOOP_PRINTF(cnx, "ADD SYMBOL WITH KEY %u\n", decode_u32(rs->metadata.fss.val));
//    return pq_insert_and_pop_min_if_full(buffer->pq, rs->metadata.first_id + rs->metadata.n_protected_symbols - 1, rs);
    return pq_insert_and_pop_min_if_full(buffer->pq, decode_u32(rs->metadata.fss.val), rs);
}

// returns a symbol that has been removed if the buffer was full
static __attribute__((always_inline)) void remove_and_free_unused_repair_symbols(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer, uint32_t remove_under) {


}

// pre: symbols must have a length of at least the max size of the buffer
static __attribute__((always_inline)) int get_repair_symbols(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer, window_repair_symbol_t **symbols,
        window_source_symbol_id_t *smallest_protected, uint32_t *highest_protected, window_source_symbol_id_t highest_contiguously_received_id, uint16_t max_concerned_source_symbols) {
    if (pq_is_empty(buffer->pq))
        return 0;
    my_memset(symbols, 0, buffer->pq->max_size*sizeof(window_repair_symbol_t *));
    PROTOOP_PRINTF(cnx, "HIGHEST RECEIVED = %lu, MIN : %lu, MAX = %lu\n", highest_contiguously_received_id, pq_get_min_key(buffer->pq), pq_get_max_key(buffer->pq));
    uint64_t max_key_in_pq = pq_get_max_key(buffer->pq);
    uint64_t min_key_in_pq = pq_get_min_key(buffer->pq);
//    int added = pq_get_between_bounds(buffer->pq, MAX(min_key_in_pq, highest_contiguously_received_id), max_key_in_pq + 1, (void **) symbols);
    int added = pq_get_between_bounds(buffer->pq, min_key_in_pq, max_key_in_pq + 1, (void **) symbols);
    if (added == 0) {
        return added;
    }
    window_source_symbol_id_t min_key = symbols[0]->metadata.first_id;
    window_source_symbol_id_t max_key = symbols[0]->metadata.first_id + symbols[0]->metadata.n_protected_symbols - 1;
    PROTOOP_PRINTF(cnx, "FIRST ADDED [%u, %u]\n", symbols[0]->metadata.first_id, symbols[0]->metadata.first_id + symbols[0]->metadata.n_protected_symbols - 1);
    int n_tried = 1;
    for(int i = 1 ; n_tried < added && i < buffer->pq->max_size ; i++) {
        if (symbols[i]) {
            PROTOOP_PRINTF(cnx, "ADDED [%u, %u]\n", symbols[i]->metadata.first_id, symbols[i]->metadata.first_id + symbols[i]->metadata.n_protected_symbols - 1);
            n_tried++;
            if (symbols[i]->metadata.first_id < min_key)
                min_key = symbols[i]->metadata.first_id;
            if (symbols[i]->metadata.first_id + symbols[i]->metadata.n_protected_symbols - 1 > max_key)
                max_key = symbols[i]->metadata.first_id + symbols[i]->metadata.n_protected_symbols - 1;
        }
    }
    if (max_key - min_key > max_concerned_source_symbols) {
        // too much symbols concerned, we should prune and take the most recent repair symbols
        uint64_t new_min_key = 0, new_max_key = 0;
        int new_added = 0;
        n_tried = 0;
        for (int i = 0 ; n_tried < added && i < buffer->pq->max_size ; i++) {
            if (symbols[i]) {
                if (highest_contiguously_received_id > symbols[i]->metadata.first_id + symbols[i]->metadata.n_protected_symbols - 1 || symbols[i]->metadata.first_id <= max_key - max_concerned_source_symbols) {
                    symbols[i] = NULL;
                } else {
                    if (new_min_key == 0 || symbols[i]->metadata.first_id < new_min_key)
                        new_min_key = symbols[i]->metadata.first_id;
                    if (new_max_key == 0 || symbols[i]->metadata.first_id + symbols[i]->metadata.n_protected_symbols - 1 > new_max_key)
                        new_max_key = symbols[i]->metadata.first_id + symbols[i]->metadata.n_protected_symbols - 1;
                    n_tried++;
                    new_added++;
                }
            }
        }
        added = new_added;
        min_key = new_min_key;
        max_key = new_max_key;
    }
    *smallest_protected = min_key;
    *highest_protected = max_key;
    return added;
}

#endif //PICOQUIC_WINDOW_RECEIVE_BUFFERS_H
