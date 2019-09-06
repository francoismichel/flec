#include "../../helpers.h"
#include "window_receive_buffers.h"
#include "types.h"

#define MAX_RECEIVE_BUFFER_SIZE 110


#define MIN(a, b) ((a < b) ? a : b)


#define MAX_AMBIGUOUS_ID_GAP ((uint32_t) 0x200*2)       // the max ambiguous ID gap depends on the max number of source symbols that can be protected by a Repair Symbol


typedef struct {
    uint32_t highest_removed;
    uint32_t highest_contiguous_received;
    fec_scheme_t fs;
    received_source_symbols_buffer_t *received_source_symbols;
    received_repair_symbols_buffer_t *received_repair_symbols;

    window_repair_symbol_t *rs_recovery_buffer;
    window_source_symbol_t **ss_recovery_buffer;
    window_source_symbol_id_t *missing_symbols_buffer;
    
} window_fec_framework_receiver_t;

static __attribute__((always_inline)) window_fec_framework_receiver_t *create_framework_receiver(picoquic_cnx_t *cnx, fec_scheme_t fs) {
    window_fec_framework_receiver_t *wff = my_malloc(cnx, sizeof(window_fec_framework_receiver_t));
    if (!wff)
        return NULL;
    my_memset(wff, 0, sizeof(window_fec_framework_receiver_t));
    wff->fs = fs;
    wff->received_source_symbols = new_source_symbols_buffer(cnx, MAX_RECEIVE_BUFFER_SIZE);
    if (!wff->received_source_symbols) {
        my_free(cnx, wff);
        return NULL;
    }
    wff->received_repair_symbols = new_repair_symbols_buffer(cnx, MAX_RECEIVE_BUFFER_SIZE);
    if (!wff->received_repair_symbols) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        my_free(cnx, wff);
        return NULL;
    }
    wff->ss_recovery_buffer = my_malloc(cnx, MAX_RECEIVE_BUFFER_SIZE*sizeof(window_source_symbol_t *));
    if (!wff->ss_recovery_buffer) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        my_free(cnx, wff);
        return NULL;
    }
    wff->rs_recovery_buffer = my_malloc(cnx, MAX_RECEIVE_BUFFER_SIZE*sizeof(window_repair_symbol_t *));
    if (!wff->rs_recovery_buffer) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        my_free(cnx, wff->ss_recovery_buffer);
        my_free(cnx, wff);
        return NULL;
    }
    wff->missing_symbols_buffer = my_malloc(cnx, MAX_RECEIVE_BUFFER_SIZE*sizeof(window_source_symbol_id_t));
    if (!wff->missing_symbols_buffer) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        my_free(cnx, wff->ss_recovery_buffer);
        my_free(cnx, wff->rs_recovery_buffer);
        my_free(cnx, wff);
        return NULL;
    }
    return wff;
}

// returns true if it has changed
static __attribute__((always_inline)) bool update_highest_contiguous_received(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff) {
    if (wff->received_source_symbols->size > 0 && ((int64_t) get_last_source_symbol_id(cnx, wff->received_source_symbols).raw) - (int64_t) wff->received_source_symbols->max_size > wff->highest_contiguous_received)
        wff->highest_contiguous_received = get_last_source_symbol_id(cnx, wff->received_source_symbols).raw - wff->received_source_symbols->max_size;
    if (wff->highest_contiguous_received > 0 && wff->received_source_symbols->size > 0 && wff->highest_contiguous_received < get_first_source_symbol_id(cnx, wff->received_source_symbols).raw) {
        return false;
    }

    uint32_t old_val = wff->highest_contiguous_received;
    while(buffer_contains_source_symbol(cnx, wff->received_source_symbols, wff->highest_contiguous_received + 1))
        wff->highest_contiguous_received++;
    return old_val != wff->highest_contiguous_received;
}

// pre: buffers sizes == MAX_RECEIVE_BUFFER_SIZE
static __attribute__((always_inline)) void select_source_and_repair_symbols(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t **source_symbols,
                                                                            uint16_t *n_source_symbols, window_source_symbol_id_t *first_symbol_id,
                                                                            window_repair_symbol_t *repair_symbols, uint16_t *n_repair_symbols, uint16_t *n_missing_source_symbols,
                                                                            window_source_symbol_id_t *missing_source_symbols) {

}


static __attribute__((always_inline)) void recover_lost_symbols(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t **source_symbols,
            uint16_t n_source_symbols, window_source_symbol_id_t first_symbol_id, window_repair_symbol_t *repair_symbols, uint16_t n_repair_symbols,
            uint16_t n_missing_source_symbols) {


}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) bool window_receive_source_symbol(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t *ss, window_source_symbol_id_t id){
    window_source_symbol_id_t removed_id = 0;
    source_symbol_t *removed = add_source_symbol(cnx, wff->received_source_symbols, ss, id, &removed_id);
    if (removed) {
        wff->highest_removed = MAX(removed_id, wff->highest_removed);
        delete_source_symbol(cnx, removed);
    }
    // let's find all the blocks protecting this symbol to see if we can recover the remaining
    // we don't recover symbols if we already are in recovery mode
    if (update_highest_contiguous_received(cnx, wff))
        remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, wff->highest_contiguous_received + 1);
    return true;
}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int window_receive_repair_symbol(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_repair_symbol_t *rs) {
    if (rs->metadata.first_id + rs->metadata.n_protected_symbols - 1 <= wff->highest_contiguous_received)
        return false;
    // TODO: encode fec/fb-fec information
//    rs->fec_scheme_specific &= 0x7FFFFFFFU;
    repair_symbol_t *removed = add_repair_symbol(cnx, wff->received_repair_symbols, rs);
    if (removed)
        delete_repair_symbol(cnx, removed);
//    try_to_recover(cnx, wff);
    return true;
}


static __attribute__((always_inline)) void try_to_recover(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff) {
    if (update_highest_contiguous_received(cnx, wff))
        remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, wff->highest_contiguous_received + 1);
    uint16_t selected_source_symbols = 0, selected_repair_symbols = 0, missing_source_symbols = 0;
    window_source_symbol_id_t first_selected_id = 0;
    select_source_and_repair_symbols(cnx, wff, wff->ss_recovery_buffer, &selected_source_symbols, &first_selected_id, wff->rs_recovery_buffer,
            &selected_repair_symbols, &missing_source_symbols, wff->missing_symbols_buffer);
    if (selected_source_symbols == 0 || selected_repair_symbols == 0 ||
        missing_source_symbols == 0)
        return;
    // we here assume that the first protected symbol is encoded in the fec block number
    if (first_selected_id > wff->highest_removed && selected_repair_symbols >= missing_source_symbols) {
        recover_lost_symbols(cnx, wff, wff->ss_recovery_buffer, selected_source_symbols, first_selected_id, wff->rs_recovery_buffer, selected_repair_symbols, missing_source_symbols);
        // we don't free anything, it will be freed when new symbols are received
    }

    for (int i = 0 ; i < missing_source_symbols ; i++) {
        window_source_symbol_t *ss = wff->ss_recovery_buffer[wff->missing_symbols_buffer[i] - first_selected_id];
        if (ss) {
            window_receive_source_symbol(cnx, wff, ss, wff->missing_symbols_buffer[i]);
        }
    }


}
