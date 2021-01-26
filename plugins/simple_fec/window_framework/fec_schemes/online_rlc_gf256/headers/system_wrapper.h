
#ifndef ONLINE_GAUSSIAN_SYSTEM_WRAPPER_H
#define ONLINE_GAUSSIAN_SYSTEM_WRAPPER_H

#include "system.h"
#include "../gf256/swif_symbol.h"
#include "util.h"
#include "arraylist.h"
#include "../../../window_receive_buffers.h"

typedef struct {
    system_t *system;
    arraylist_t unknowns_ids;
    uint8_t *inv_table;
    uint8_t **mul_table;
    arraylist_t unknown_recovered;      // the IDs contained MUST be ordered !
    arraylist_t temp_arraylist;
    arraylist_t recovered_ids_arraylist;
} system_wrapper_t;



static __attribute__((always_inline)) int wrapper_init(picoquic_cnx_t *cnx, system_wrapper_t *wrapper, system_t *system, uint8_t *inv_table, uint8_t **mul_table) {
    wrapper->system = system;
    wrapper->inv_table = inv_table;
    wrapper->mul_table = mul_table;
    arraylist_init(cnx, &wrapper->unknowns_ids, 50);
    arraylist_init(cnx, &wrapper->unknown_recovered, 50);
    arraylist_init(cnx, &wrapper->temp_arraylist, 50);
    arraylist_init(cnx, &wrapper->recovered_ids_arraylist, 50);
    return 0;
}

static __attribute__((always_inline)) int id_in_unknowns(system_wrapper_t *wrapper, source_symbol_id_t id) {
    for (int i = 0 ; i < arraylist_size(&wrapper->unknowns_ids) ; i++) {
        if (arraylist_get(&wrapper->unknowns_ids, i) == id) {
            return i;
        }
    }
    return -1;
}

static __attribute__((always_inline)) int wrapper_receive_source_symbol(picoquic_cnx_t *cnx, system_wrapper_t *wrapper, window_source_symbol_t *ss, equation_t **removed, int *used_in_system) {
    *used_in_system = 0;
    int index = -1;
    if (arraylist_is_empty(&wrapper->unknowns_ids) || ss->id < arraylist_get(&wrapper->unknowns_ids, 0)) {
        // should be ignored, because unknowns_ids[0] - 1 is the highest contiguously recieved
    } else if (arraylist_is_empty(&wrapper->unknowns_ids) || ss->id > arraylist_get_last(&wrapper->unknowns_ids)) {
        // should be ignored, nothing to do right now
                                                                                                                // avoid adding if already recovered
    } else if (!arraylist_is_empty(&wrapper->unknowns_ids) && (index = id_in_unknowns(wrapper, ss->id)) != -1 && !arraylist_get(&wrapper->unknown_recovered, index)) {
        equation_t *eq = equation_create_from_source(cnx, index, ss->source_symbol._whole_data, ss->source_symbol.chunk_size+1);
        if (eq == NULL) {
            PROTOOP_PRINTF(cnx, "COULD NOT RECEIVE SOURCE SYMBOL\n");
            return PICOQUIC_ERROR_MEMORY;
        }
        PROTOOP_PRINTF(cnx, "ADD SOURCE SYMBOL %u\n", ss->id);
        // unlikely
        int decoded = 0;
        system_add_with_elimination(cnx, wrapper->system, eq, wrapper->inv_table, wrapper->mul_table, &decoded, removed, used_in_system);
        if (*used_in_system) {
            arraylist_set(&wrapper->unknown_recovered, index, true);
        }
        // TODO; handle recovered symbols
        if (!*used_in_system) {
            equation_free(cnx, eq);
        }
    }
    return 0;
}

static __attribute__((always_inline)) int
extend_wrapper_and_adjust_repair_symbol(picoquic_cnx_t *cnx, system_wrapper_t *wrapper, equation_t *rs, ring_based_received_source_symbols_buffer_t *source_symbols) {
    if (wrapper->system->first_id_id != SYMBOL_ID_NONE && rs->pivot < wrapper->system->first_id_id) {
        // the rs is too old
        PROTOOP_PRINTF(cnx, "should not happen !\n");
        return 0;
    }

    int64_t first_wrapper_id_in_new_rs = -1;
//    PROTOOP_PRINTF(cnx, "BEFORE ADJUST LOOP, FIRST ID = %u, LAST ID = %u, N UNKNUWNS = %u\n", rs->constant_term.metadata.first_id, repair_symbol_last_id(&rs->constant_term), arraylist_size(&wrapper->temp_arraylist));
    for (int i = 0 ; i < arraylist_size(&wrapper->temp_arraylist) ; i++) {
        source_symbol_id_t current_id = arraylist_get(&wrapper->temp_arraylist, i);
        if (arraylist_is_empty(&wrapper->unknowns_ids) || arraylist_get_last(&wrapper->unknowns_ids) < current_id) {
            // add all the missing source symbols from the last considered in our system to the current index
            window_source_symbol_id_t initial_id = MAX(arraylist_get_last(&wrapper->unknowns_ids) + 1, ring_based_source_symbols_buffer_get_first_source_symbol_id(cnx, source_symbols));
            for (source_symbol_id_t id =  initial_id ; id <= current_id ; id++) {
                if (!ring_based_source_symbols_buffer_contains(cnx, source_symbols, id)) {
                    // the symbol is missing
                    arraylist_push(cnx, &wrapper->unknowns_ids, id);
                    arraylist_push(cnx, &wrapper->unknown_recovered, false);
                }
            }
            if (first_wrapper_id_in_new_rs == -1 && !arraylist_is_empty(&wrapper->unknowns_ids) && arraylist_get_last(&wrapper->unknowns_ids) < arraylist_get_first(&wrapper->temp_arraylist)) {
                first_wrapper_id_in_new_rs = arraylist_size(&wrapper->unknowns_ids) - 1;
            }
        } else if (current_id < arraylist_get_first(&wrapper->unknowns_ids)) {
            PROTOOP_PRINTF(cnx, "should not happen !\n");
            return -1;
        }
    }
//    PROTOOP_PRINTF(cnx, "AFTER ADJUST LOOP\n");
    if (arraylist_size(&wrapper->unknowns_ids) == 0) {
        return 0;
    }
    if (first_wrapper_id_in_new_rs == -1) {
//        PROTOOP_PRINTF(cnx, "FIRT WRAPPER ID IS -1, FIRST IN UNKNOWNS = %u\n", arraylist_get_first(&wrapper->unknowns_ids));
        first_wrapper_id_in_new_rs = arraylist_index(&wrapper->unknowns_ids, arraylist_get_first(&wrapper->temp_arraylist));
    }
//    PROTOOP_PRINTF(cnx, "FIRST WRAPPER ID IN NEW RS = %d\n", first_wrapper_id_in_new_rs);
    if (wrapper->system->first_id_id != SYMBOL_ID_NONE) {
        first_wrapper_id_in_new_rs += wrapper->system->first_id_id;
    }
    int index_of_first_unknown = arraylist_index(&wrapper->unknowns_ids, arraylist_get_first(&wrapper->temp_arraylist));
    int index_of_last_unknown = arraylist_index(&wrapper->unknowns_ids, arraylist_get_last(&wrapper->temp_arraylist));
    uint8_t *temp_coefs = my_malloc(cnx, rs->_coefs_allocated_size);
    my_memcpy(temp_coefs, rs->coefs, rs->_coefs_allocated_size);
    my_memset(rs->coefs, 0, rs->_coefs_allocated_size);
    // build the new ID with coefs concerning only the lost symbols
    for(int i = 0 ; i < arraylist_size(&wrapper->temp_arraylist) ; i++) {
        source_symbol_id_t current_id = arraylist_get(&wrapper->temp_arraylist, i);
        // reset the coefs of the rs progressively
        int index_in_unknowns = arraylist_index(&wrapper->unknowns_ids, current_id);
        rs->coefs[index_in_unknowns - index_of_first_unknown] = temp_coefs[current_id - rs->constant_term.metadata.first_id];
    }
    rs->constant_term.metadata.first_id = first_wrapper_id_in_new_rs;
    rs->pivot = first_wrapper_id_in_new_rs;
    rs->constant_term.metadata.n_protected_symbols = (index_of_last_unknown + 1) - index_of_first_unknown; //arraylist_size(&wrapper->temp_arraylist);
    memset_fn(&rs->coefs[rs->constant_term.metadata.n_protected_symbols], 0, rs->n_coefs - rs->constant_term.metadata.n_protected_symbols);

    my_free(cnx, temp_coefs);
    equation_adjust_non_zero_bounds(rs);
// the rs has been wrapped !
    return 0;
}

static __attribute__((always_inline)) int wrapper_receive_repair_symbol(picoquic_cnx_t *cnx, system_wrapper_t *wrapper, equation_t *rs, ring_based_received_source_symbols_buffer_t *source_symbols, equation_t **removed, int *used_in_system) {
    arraylist_reset(&wrapper->temp_arraylist);

    *removed = NULL;
    *used_in_system = 0;

    window_source_symbol_id_t last_id_to_check = repair_symbol_last_id(&rs->constant_term);
    for (source_symbol_id_t i = rs->constant_term.metadata.first_id ; i <= last_id_to_check ; i++) {
        window_source_symbol_id_t idx = i - rs->constant_term.metadata.first_id;
        if (ring_based_source_symbols_buffer_contains(cnx, source_symbols, i)) {
            symbol_add_scaled(rs->constant_term.repair_symbol.repair_payload, rs->coefs[idx], ring_based_source_symbols_buffer_get(cnx, source_symbols, i)->source_symbol._whole_data, align(rs->constant_term.repair_symbol.payload_length), wrapper->mul_table);
            rs->coefs[idx] = 0;
        } else {
            arraylist_push(cnx, &wrapper->temp_arraylist, i);
        }
    }
//    PROTOOP_PRINTF(cnx, "BEFORE ADJUST BOUNDS\n");

    equation_adjust_non_zero_bounds(rs);
    if (equation_has_one_id(rs)) {
        equation_multiply(rs, wrapper->inv_table[equation_get_coef(rs, rs->pivot)], wrapper->mul_table);
        PROTOOP_PRINTF(cnx, "RS HAS ONE ID FOR %u\n", rs->pivot);
    }
    if (arraylist_is_empty(&wrapper->temp_arraylist)) {
        PROTOOP_PRINTF(cnx, "ignore source symbol\n");
        return 0;
    }
    extend_wrapper_and_adjust_repair_symbol(cnx, wrapper, rs, source_symbols);

    if (wrapper->system->first_id_id == SYMBOL_ID_NONE && rs->pivot != 0) {
        // this can happen when there are unknown source symbols for which no repair symbol has been received yet
        // in that case, we add the knowledge of symbols from 0 to pivot - 1 to the system
        system_set_bounds(cnx, wrapper->system, 0, rs->pivot-1);
    }

    int decoded = 0;

    system_add_with_elimination(cnx, wrapper->system, rs, wrapper->inv_table, wrapper->mul_table, &decoded, removed, used_in_system);
    if (decoded) {
        int n_non_null_equations = 0;
        for (int i = 0 ; i < wrapper->system->max_equations && n_non_null_equations < wrapper->system->n_equations && i < arraylist_size(&wrapper->unknowns_ids); i++) {
            equation_t *eq = wrapper->system->equations[i];
            if (eq != NULL) {
                n_non_null_equations++;
                if (!arraylist_get(&wrapper->unknown_recovered, i) && equation_has_one_id(eq)) {
                    arraylist_set(&wrapper->unknown_recovered, i, true);
                    assert(arraylist_get(&wrapper->unknowns_ids, i) != 0);
                    arraylist_push(cnx, &wrapper->recovered_ids_arraylist, arraylist_get(&wrapper->unknowns_ids, i));
                }
            }
        }
    }
    // TODO: update rs bounds to match to the wrapped idx
    return 0;
}

static __attribute__((always_inline)) int wrapper_remove_recovered_symbols(picoquic_cnx_t *cnx, system_wrapper_t *wrapper, source_symbol_id_t id) {
    if (arraylist_size(&wrapper->unknowns_ids) == 0 || id < arraylist_get_first(&wrapper->unknowns_ids)) {
        return 0;
    }
    int index = -1;// arraylist_index(&wrapper->unknowns_ids, id);

    for (int i = 0 ; i < arraylist_size(&wrapper->unknowns_ids) ; i++) {
        if (arraylist_get(&wrapper->unknowns_ids, i) <= id) {
            index = i;
        } else {
            break;
        }
    }
    if (index == -1) {
        return -1;
    }
    source_symbol_id_t first_id_before = wrapper->system->first_id_id;
    int err = remove_all_pivots_until(cnx, wrapper->system, index + wrapper->system->first_id_id);
    if (err) {
        return err;
    }
    if (wrapper->system->first_id_id == SYMBOL_ID_NONE) {
        // the system has been emptied
        arraylist_reset(&wrapper->unknowns_ids);
        arraylist_reset(&wrapper->unknown_recovered);
    } else {
        source_symbol_id_t n_removed_unknowns = wrapper->system->first_id_id - first_id_before;
        arraylist_shift_left(cnx, &wrapper->unknowns_ids, n_removed_unknowns);
        arraylist_shift_left(cnx, &wrapper->unknown_recovered, n_removed_unknowns);
    }

    return 0;
}
#endif //ONLINE_GAUSSIAN_SYSTEM_WRAPPER_H
