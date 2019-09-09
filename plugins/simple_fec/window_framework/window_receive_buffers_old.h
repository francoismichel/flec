#ifndef PICOQUIC_WINDOW_RECEIVE_BUFFERS_OLD_H
#define PICOQUIC_WINDOW_RECEIVE_BUFFERS_H

#include "../fec.h"
#include "types.h"

typedef struct {
    window_source_symbol_t **source_symbols;
    int first;
    int last;
    int size;
    uint32_t max_size;
} received_source_symbols_buffer_t;

static __attribute__((always_inline)) received_source_symbols_buffer_t *new_source_symbols_buffer(picoquic_cnx_t *cnx,
                                                                                                  int max_size) {
    window_source_symbol_t **symbols = my_malloc(cnx, max_size*sizeof(window_source_symbol_t *));
    if (!symbols)
        return NULL;
    my_memset(symbols, 0, max_size*sizeof(window_source_symbol_t *));
    received_source_symbols_buffer_t *buffer = my_malloc(cnx, sizeof(received_source_symbols_buffer_t));
    if (!buffer) {
        my_free(cnx, symbols);
        return NULL;
    }
    my_memset(buffer, 0, sizeof(received_source_symbols_buffer_t));
    buffer->max_size = max_size;
    buffer->source_symbols = symbols;
    buffer->first = -1;
    buffer->last = -1;
    return buffer;
}

static __attribute__((always_inline)) void release_source_symbols_buffer(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer) {
    my_free(cnx, buffer->source_symbols);
    my_free(cnx, buffer);
}

static __attribute__((always_inline)) bool buffer_contains_source_symbol(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer, window_source_symbol_id_t id) {
    if (buffer->size == 0)
        return false;
    uint32_t idx = id % buffer->max_size;
    return buffer->source_symbols[idx] && buffer->source_symbols[idx]->id == id;
}

// returns the symbol that has been removed if the buffer was full
static __attribute__((always_inline)) window_source_symbol_t *add_source_symbol(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer, window_source_symbol_t *ss,
        window_source_symbol_id_t id, window_source_symbol_id_t *removed_id) {
    if (!ss || buffer_contains_source_symbol(cnx, buffer, ss->id))
        return NULL;
    uint32_t idx = ss->id % buffer->max_size;
    window_source_symbol_t *retval = buffer->source_symbols[idx];
    if (!retval)
        buffer->size++;
    else if (retval->id >= ss->id)
        // a more recent symbol is already present
        return NULL;
    buffer->source_symbols[idx] = ss;

    if (buffer->last < 0 || ss->id > buffer->source_symbols[buffer->last]->id)
        buffer->last = idx;

    // find the new first if needed
    if (buffer->first < 0 || (ss->id < buffer->source_symbols[buffer->first]->id && idx != buffer->first))
        buffer->first = idx;
    else if (retval && idx == buffer->first && buffer->size > 1) {
        uint32_t n_considered_symbols = 1 + ((buffer->first < buffer->last) ? (buffer->last - buffer->first) : (buffer->last + buffer->max_size - buffer->first));
        uint32_t n_considered = 0;
        int min_index = -1;
        uint32_t min_val = 0;
        for (uint32_t i = 0 ; i < buffer->max_size - 1 && n_considered < buffer->max_size ; i++) {
            int index = (((uint32_t) buffer->first) + 1U + i) % buffer->max_size;
            if (buffer->source_symbols[index]) {
                n_considered++;
                if (min_index == -1 || buffer->source_symbols[index]->id < min_val) {
                    min_index = index;
                    min_val = buffer->source_symbols[index]->id;
                }
            }
        }
        buffer->first = min_index;
    }


    return retval;
}

// pre: the buffer should not be empty
static __attribute__((always_inline)) source_fpid_t get_first_source_symbol_id(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer) {
    return buffer->source_symbols[buffer->first]->source_fec_payload_id;
}

// pre: the buffer should not be empty
static __attribute__((always_inline)) source_fpid_t get_last_source_symbol_id(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer) {
    return buffer->source_symbols[buffer->last]->source_fec_payload_id;
}

// inserts the source symbols in the provided buffer starting with the symbol with the smallest sfpid set at the first entry of the array and sets NULL when source symbols are missing
// returns the total number of considered symbols, including the missing ones
static __attribute__((always_inline)) int get_source_symbols(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer, window_source_symbol_t **symbols, int max_elements) {
    if (buffer->size == 0)
        return 0;
    int added = 0;
    int total = 0;
    for (uint32_t i = 0 ; added < buffer->size && added < max_elements ; i++) {
        uint32_t idx = (buffer->first + i) % buffer->max_size;

        symbols[i] = buffer->source_symbols[idx];
        if (buffer->source_symbols[idx])
            added++;
        total++;
    }
    return total;
}

// inserts the source symbols in the provided buffer starting with the symbol with the smallest sfpid set at the first entry of the array and sets NULL when source symbols are missing
// returns the number of received source symbols (total - missing ones)
static __attribute__((always_inline)) int get_source_symbols_between_bounds(picoquic_cnx_t *cnx, received_source_symbols_buffer_t *buffer, window_source_symbol_t **symbols, int max_elements, uint32_t min_symbol, uint32_t max_symbol) {
    if (buffer->size == 0)
        return 0;
    int added = 0;
    int total = 0;
    for (uint32_t i = min_symbol ; i <= max_symbol && i - min_symbol < max_elements ; i++) {
        uint32_t idx = (i % buffer->max_size);
        symbols[i - min_symbol] = NULL;
        if (buffer->source_symbols[idx] && buffer->source_symbols[idx]->source_fec_payload_id.raw == i) {
            symbols[i - min_symbol] = buffer->source_symbols[idx];
            added++;
        }
        total++;
    }
    return added;
}

typedef struct {
    window_repair_symbol_t *symbols;
    int first;
    int last;
    int size;
    uint32_t max_size;
} received_repair_symbols_buffer_t;



static __attribute__((always_inline)) received_repair_symbols_buffer_t *new_repair_symbols_buffer(picoquic_cnx_t *cnx,
                                                                                                  int max_size) {
    repair_symbol_t **symbols = my_malloc(cnx, max_size*sizeof(repair_symbol_t *));
    if (!symbols)
        return NULL;
    my_memset(symbols, 0, max_size*sizeof(repair_symbol_t *));
    uint16_t *nss = my_malloc(cnx, max_size*sizeof(uint16_t));
    if (!nss) {
        my_free(cnx, symbols);
        return NULL;
    }
    my_memset(symbols, 0, max_size*sizeof(repair_symbol_t *));
    received_repair_symbols_buffer_t *buffer = my_malloc(cnx, sizeof(received_repair_symbols_buffer_t));
    if (!buffer) {
        my_free(cnx, symbols);
        my_free(cnx, nss);
        return NULL;
    }
    my_memset(buffer, 0, sizeof(received_repair_symbols_buffer_t));
    buffer->max_size = max_size;
    buffer->repair_symbols = symbols;
    buffer->nss = nss;
    buffer->first = -1;
    buffer->last = -1;
    return buffer;
}

static __attribute__((always_inline)) void release_repair_symbols_buffer(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer) {
    my_free(cnx, buffer->repair_symbols);
    my_free(cnx, buffer->nss);
    my_free(cnx, buffer);
}

// returns the symbol that has been removed if the buffer was full
static __attribute__((always_inline)) repair_symbol_t *add_repair_symbol(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer, window_repair_symbol_t *rs) {
    // FIXME: do a simple ring buffer
    uint32_t to_add_idx = rs->metadata.fss. % buffer->max_size;
    repair_symbol_t *retval = buffer->repair_symbols[to_add_idx];
    buffer->repair_symbols[to_add_idx] = rs;
    buffer->nss[to_add_idx] = nss;
    if (!retval)
        buffer->size++;


    if (buffer->last < 0 || rs->fec_scheme_specific > buffer->repair_symbols[buffer->last]->fec_scheme_specific)
        buffer->last = to_add_idx;
    // find the new first if needed
    if (buffer->first < 0 || rs->fec_scheme_specific < buffer->repair_symbols[buffer->first]->fec_scheme_specific)
        buffer->first = to_add_idx;
    else if (retval && to_add_idx == buffer->first && buffer->max_size > 1) {
        buffer->first = -1;
        int64_t current_min = -1;
        int n_considered = 0;
        // find the new first
        for (uint32_t i = 0; i < buffer->max_size && n_considered < buffer->size; i++) {
            uint32_t idx = ((uint32_t) to_add_idx + 1 + i) % buffer->max_size;
            if (buffer->repair_symbols[idx]) {
                n_considered++;
                if (current_min == -1 || buffer->repair_symbols[idx]->fec_scheme_specific < current_min) {
                    buffer->first = idx;
                    current_min = buffer->repair_symbols[idx]->fec_scheme_specific;
                }
            }
        }
    }
    return retval;
}

// returns a symbol that has been removed if the buffer was full
static __attribute__((always_inline)) void remove_and_free_unused_repair_symbols(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer, uint32_t remove_under) {
    PROTOOP_PRINTF(cnx, "REMOVE AND FREE UNDER %u\n", remove_under);
    int64_t last_removed = -1;
    uint32_t n_considered_symbols = 0;
    if (buffer->first != -1 && buffer->last != -1) {
        n_considered_symbols = 1 + ((buffer->first <= buffer->last) ? (buffer->last - buffer->first) : (buffer->last + buffer->max_size - buffer->first));
    }
    PROTOOP_PRINTF(cnx, "CONSIDER %d SYMBOLS\n", n_considered_symbols);
    for (uint32_t i = 0 ; i < n_considered_symbols ; i++) {
        uint32_t idx = ((uint32_t) buffer->first + i) % buffer->max_size;
        repair_symbol_t *rs = buffer->repair_symbols[idx];
        if (rs) {
            uint32_t first_protected = rs->repair_fec_payload_id.source_fpid.raw;
            uint32_t last_protected = first_protected + buffer->nss[idx] - 1;
            if (last_protected < remove_under) {
                PROTOOP_PRINTF(cnx, "REMOVE RS, IDs = %u\n", first_protected);
                free_repair_symbol(cnx, buffer->repair_symbols[idx]);
                buffer->repair_symbols[idx] = NULL;
                buffer->size--;
                last_removed = idx;
                if (idx == buffer->last)
                    buffer->last = -1;
            }
        }
    }
    PROTOOP_PRINTF(cnx, "LAST REMOVED = %u\n", last_removed);
    if (last_removed > -1) {
        buffer->first = -1;
        int64_t current_min = -1;
        int n_considered = 0;
        PROTOOP_PRINTF(cnx, "MAX SIZE = %d, CURRENT SIZE = %d\n", buffer->max_size, buffer->size);
        // find the new first
        for (uint32_t i = 0 ; i < buffer->max_size && n_considered < buffer->size ; i++) {
            uint32_t idx = ((uint32_t) last_removed + 1 + i) % buffer->max_size;
            if (buffer->repair_symbols[idx]) {
                n_considered++;
                PROTOOP_PRINTF(cnx, "CONSIDER %u, current_min = %ld\n", buffer->repair_symbols[idx]->fec_scheme_specific, current_min);
                if (current_min == -1 || buffer->repair_symbols[idx]->fec_scheme_specific < current_min) {
                    buffer->first = idx;
                    current_min = buffer->repair_symbols[idx]->fec_scheme_specific;
                    PROTOOP_PRINTF(cnx, "NEW MIN = %u\n", current_min);
                }
            }
        }
    }
}


// returns the symbol that has been removed if the buffer was full
static __attribute__((always_inline)) int get_repair_symbols(picoquic_cnx_t *cnx, received_repair_symbols_buffer_t *buffer, repair_symbol_t **symbols, int max_elems, uint32_t *smallest_protected, uint32_t *highest_protected) {
    if (buffer->size == 0)
        return 0;
    int added = 0;
    *highest_protected = *smallest_protected = 0;
    uint32_t n_considered_symbols = 0;
    if (buffer->first != -1 && buffer->last != -1) {
        n_considered_symbols = 1 + ((buffer->first <= buffer->last) ? (buffer->last - buffer->first) : (buffer->last + buffer->max_size - buffer->first));
    }
    for (uint32_t i = 0 ; i < n_considered_symbols && added < max_elems ; i++) {
        uint32_t idx = (buffer->first + i) % buffer->max_size;
        if (buffer->repair_symbols[idx]) {
            PROTOOP_PRINTF(cnx, "GET RS %p AT %d, IDs = [%u, %u], FSS = %u\n", (protoop_arg_t) buffer->repair_symbols[idx], idx, buffer->repair_symbols[idx]->repair_fec_payload_id.source_fpid.raw, buffer->repair_symbols[idx]->repair_fec_payload_id.source_fpid.raw + buffer->repair_symbols[idx]->nss - 1, buffer->repair_symbols[idx]->fec_scheme_specific);
            symbols[added++] = buffer->repair_symbols[idx];
            *highest_protected = MAX(*highest_protected, buffer->repair_symbols[idx]->repair_fec_payload_id.source_fpid.raw + buffer->repair_symbols[idx]->nss - 1);
        }
    }
    *smallest_protected = buffer->repair_symbols[buffer->first]->repair_fec_payload_id.source_fpid.raw;
    return added;
}

#endif //PICOQUIC_WINDOW_RECEIVE_BUFFERS_OLD_H
