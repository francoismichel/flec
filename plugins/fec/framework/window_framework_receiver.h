#include "../fec_protoops.h"
#include "../../helpers.h"
#include "window_receive_buffers.h"


#define MIN(a, b) ((a < b) ? a : b)


#define MAX_AMBIGUOUS_ID_GAP ((uint32_t) 0x200*2)       // the max ambiguous ID gap depends on the max number of source symbols that can be protected by a Repair Symbol


typedef struct {
    uint32_t highest_removed;
    uint32_t highest_contiguous_received;
    fec_scheme_t fs;
    received_source_symbols_buffer_t *received_source_symbols;
    received_repair_symbols_buffer_t *received_repair_symbols;

    repair_symbol_t **rs_recovery_buffer;
    source_symbol_t **ss_recovery_buffer;
    fec_block_t fec_block_buffer;
} window_fec_framework_receiver_t;

static __attribute__((always_inline)) window_fec_framework_receiver_t *create_framework_receiver(picoquic_cnx_t *cnx, fec_scheme_t fs) {
    window_fec_framework_receiver_t *wff = my_malloc(cnx, sizeof(window_fec_framework_receiver_t));
    if (!wff)
        return NULL;
    my_memset(wff, 0, sizeof(window_fec_framework_receiver_t));
    wff->fs = fs;
    wff->received_source_symbols = new_source_symbols_buffer(cnx, RECEIVE_BUFFER_MAX_LENGTH);
    if (!wff->received_source_symbols) {
        my_free(cnx, wff);
        return NULL;
    }
    wff->received_repair_symbols = new_repair_symbols_buffer(cnx, RECEIVE_BUFFER_MAX_LENGTH);
    if (!wff->received_repair_symbols) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        my_free(cnx, wff);
        return NULL;
    }
    wff->ss_recovery_buffer = my_malloc(cnx, RECEIVE_BUFFER_MAX_LENGTH*sizeof(source_symbol_t *));
    if (!wff->ss_recovery_buffer) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        my_free(cnx, wff);
        return NULL;
    }
    wff->rs_recovery_buffer = my_malloc(cnx, RECEIVE_BUFFER_MAX_LENGTH*sizeof(repair_symbol_t *));
    if (!wff->rs_recovery_buffer) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        my_free(cnx, wff->ss_recovery_buffer);
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
        PROTOOP_PRINTF(cnx, "RETURN; FIRST SYMBOL ID = %u\n", get_first_source_symbol_id(cnx, wff->received_source_symbols).raw);
        return false;
    }

    uint32_t old_val = wff->highest_contiguous_received;
    while(buffer_contains_source_symbol(cnx, wff->received_source_symbols, wff->highest_contiguous_received + 1))
        wff->highest_contiguous_received++;
    PROTOOP_PRINTF(cnx, "UPDATE HIGHEST RECEIVED, OLD = %u, NEW = %d, FIRST SYMBOL = ?\n", old_val, wff->highest_contiguous_received);
    return old_val != wff->highest_contiguous_received;
}

static __attribute__((always_inline)) void populate_fec_block(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, fec_block_t *fb) {
    uint32_t smallest_protected = 0;
    uint32_t highest_protected = 0;
    fb->current_repair_symbols = get_repair_symbols(cnx, wff->received_repair_symbols, fb->repair_symbols, sizeof(fb->repair_symbols)/sizeof(repair_symbol_t *), &smallest_protected, &highest_protected);
    // as we work in a rateless manner, there is no total number of repair symbols, so we set it equal to the current number
    fb->total_repair_symbols = fb->current_repair_symbols;
    fb->current_source_symbols = get_source_symbols_between_bounds(cnx, wff->received_source_symbols, fb->source_symbols, sizeof(fb->source_symbols)/sizeof(source_symbol_t *), smallest_protected, highest_protected);
    fb->total_source_symbols = (highest_protected - smallest_protected) + 1;
    fb->fec_block_number = smallest_protected;
}


static __attribute__((always_inline)) void try_to_recover(picoquic_cnx_t *cnx, bpf_state *state, window_fec_framework_receiver_t *wff) {
    PROTOOP_PRINTF(cnx, "TRY TO RECOVER\n");
    if (update_highest_contiguous_received(cnx, wff))
        remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, wff->highest_contiguous_received + 1);
    PROTOOP_PRINTF(cnx, "BEFORE POPULATE\n");
    populate_fec_block(cnx, wff, &wff->fec_block_buffer);
    PROTOOP_PRINTF(cnx, "TOTAL SS = %d, CURRENT RS = %d, CURRENT SS = %d\n", wff->fec_block_buffer.total_source_symbols, wff->fec_block_buffer.current_repair_symbols, wff->fec_block_buffer.current_source_symbols);
    if (wff->fec_block_buffer.total_source_symbols == 0 || wff->fec_block_buffer.current_repair_symbols == 0 ||
        wff->fec_block_buffer.current_source_symbols == wff->fec_block_buffer.total_source_symbols)
        return;
    // we here assume that the first protected symbol is encoded in the fec block number
    uint32_t smallest_protected_id = wff->fec_block_buffer.fec_block_number;
    if (smallest_protected_id > wff->highest_removed && wff->fec_block_buffer.current_source_symbols + wff->fec_block_buffer.current_repair_symbols >= wff->fec_block_buffer.total_source_symbols) {
        recover_block(cnx, state, &wff->fec_block_buffer);
        // we don't free anything, it will be freed when new symbols are received
    }
}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int window_receive_repair_symbol(picoquic_cnx_t *cnx, bpf_state *state, window_fec_framework_receiver_t *wff, repair_symbol_t *rs, uint8_t nss, uint8_t nrs){
    PROTOOP_PRINTF(cnx, "RECEIVE REPAIR SYMBOL, FSS = %u, NSS = %d\n", rs->repair_fec_payload_id.fec_scheme_specific, rs->nss);
    PROTOOP_PRINTF(cnx, "%u + %u - 1 <= ? %u\n", rs->repair_fec_payload_id.source_fpid.raw, rs->nss, wff->highest_contiguous_received);
    if (rs->repair_fec_payload_id.source_fpid.raw + rs->nss - 1 <= wff->highest_contiguous_received)
        return false;
    rs->fec_scheme_specific &= 0x7FFFFFFFU;
    repair_symbol_t *removed = add_repair_symbol(cnx, wff->received_repair_symbols, rs, nss);
    if (removed)
        free_repair_symbol(cnx, removed);
    try_to_recover(cnx, state, wff);
    return true;
}

static __attribute__((always_inline)) void try_to_recover_from_symbol(picoquic_cnx_t *cnx, bpf_state *state, window_fec_framework_receiver_t *wff, source_symbol_t *ss) {
    if (ss->source_fec_payload_id.raw <= wff->highest_removed)
        return;
    for (int i = 0 ; i < MAX_FEC_BLOCKS ; i++) {
        if (state->fec_blocks[i]) {
            fec_block_t *fb = state->fec_blocks[i];
            if (fb->fec_block_number <= ss->source_fec_payload_id.raw && ss->source_fec_payload_id.raw < fb->fec_block_number + fb->total_source_symbols) {
                // the FEC block protects this symbol
                populate_fec_block(cnx, wff, fb);
                PROTOOP_PRINTF(cnx, "RECEIVED SS %u: BLOCK = (%u, %u), CURRENT_SS = %u, CURRENT_RS = %u, TOTAL_SS = %u, TOTAL_RS = %u\n", ss->source_fec_payload_id.raw,
                               fb->fec_block_number, fb->fec_block_number+fb->total_source_symbols, fb->current_source_symbols,
                               fb->current_repair_symbols, fb->total_source_symbols, fb->total_repair_symbols);
                if (fb->current_repair_symbols > 0 && fb->current_source_symbols + fb->current_repair_symbols >= fb->total_source_symbols) {
                    recover_block(cnx, state, fb);
                    // we don't free anything, it will be free when new symbols are received
                }
            }
        }
    }
}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) bool window_receive_source_symbol(picoquic_cnx_t *cnx, bpf_state *state, window_fec_framework_receiver_t *wff, source_symbol_t *ss, bool recover){
    source_symbol_t *removed = add_source_symbol(cnx, wff->received_source_symbols, ss);
    if (removed) {
        wff->highest_removed = MAX(removed->source_fec_payload_id.raw, wff->highest_removed);
        PROTOOP_PRINTF(cnx, "FREED OLD SS %u FOR %u\n", removed->source_fec_payload_id.raw, ss->source_fec_payload_id.raw);
        free_source_symbol(cnx, removed);
    }
    // let's find all the blocks protecting this symbol to see if we can recover the remaining
    // we don't recover symbols if we already are in recovery mode
//    if (!state->in_recovery && recover) {
//        try_to_recover(cnx, state, wff);
//    } else {
        if (update_highest_contiguous_received(cnx, wff))
            remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, wff->highest_contiguous_received + 1);
        PROTOOP_PRINTF(cnx, "RECEIVED SYMBOL %u BUT DIDN'T TRY TO RECOVER\n", ss->source_fec_payload_id.raw);
//    }
    return true;
}