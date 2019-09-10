
//#ifndef PICOQUIC_FEC_WINDOW_FRAMEWORK_SENDER_H
//#define PICOQUIC_FEC_WINDOW_FRAMEWORK_SENDER_H

#include <stdint.h>
#include "../fec.h"
#include "types.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller_only_fb_fec.h"

#define INITIAL_SYMBOL_ID 1
#define MAX_QUEUED_REPAIR_SYMBOLS 6
#define MAX_SLOT_VALUE 0x7FFFFF

#define MIN(a, b) ((a < b) ? a : b)


typedef uint32_t fec_block_number;

typedef struct {
    repair_symbol_t *repair_symbol;
    window_source_symbol_id_t first_protected_symbol;
    window_fec_scheme_specific_t fss;
    uint8_t n_protected_symbols;
} queue_item;

typedef struct window_slot {
    source_symbol_t *symbol;
    source_symbol_id_t id;
    bool received;
} window_slot_t;

typedef struct {
    fec_scheme_t fec_scheme;
    window_slot_t fec_window[MAX_SENDING_WINDOW_SIZE];
    queue_item repair_symbols_queue[MAX_QUEUED_REPAIR_SYMBOLS];
    window_redundancy_controller_t controller;
    window_source_symbol_id_t max_id;
    window_source_symbol_id_t min_id;
    window_source_symbol_id_t highest_sent_id;
    window_source_symbol_id_t smallest_in_transit;
    window_source_symbol_id_t highest_in_transit;
    recovered_packets_buffer_t *rps;
    int window_length;
    uint32_t repair_symbols_queue_head;
    int repair_symbols_queue_length;
    int64_t current_slot;
} window_fec_framework_t;

static __attribute__((always_inline)) bool is_fec_window_empty(window_fec_framework_t *wff) {
    return wff->window_length == 0;
}

static __attribute__((always_inline)) window_fec_framework_t *create_framework_sender(picoquic_cnx_t *cnx, window_redundancy_controller_t controller, fec_scheme_t fs) {
    window_fec_framework_t *wff = (window_fec_framework_t *) my_malloc(cnx, sizeof(window_fec_framework_t));
    if (!wff)
        return NULL;
    my_memset(wff, 0, sizeof(window_fec_framework_t));
    wff->highest_sent_id = INITIAL_SYMBOL_ID-1;
    wff->highest_in_transit = INITIAL_SYMBOL_ID-1;
    wff->smallest_in_transit = INITIAL_SYMBOL_ID-1;
    wff->controller = controller;
    wff->fec_scheme = fs;
    wff->rps = create_recovered_packets_buffer(cnx);
    if (!wff->rps) {
        my_free(cnx, wff);
        return NULL;
    }
    return wff;
}

static __attribute__((always_inline)) bool has_repair_symbol_at_index(window_fec_framework_t *wff, int idx) {
    return wff->repair_symbols_queue_length > 0 && wff->repair_symbols_queue[idx].repair_symbol != NULL;
}

static __attribute__((always_inline)) void remove_item_at_index(picoquic_cnx_t *cnx, window_fec_framework_t *wff, int idx) {
    delete_repair_symbol(cnx, wff->repair_symbols_queue[idx].repair_symbol);
    wff->repair_symbols_queue[idx].repair_symbol = NULL;
    wff->repair_symbols_queue[idx].n_protected_symbols = 0;
}

static __attribute__((always_inline)) void put_item_at_index(window_fec_framework_t *wff, int idx, repair_symbol_t *rs, uint8_t n_protected_symbols,
        window_source_symbol_id_t first_protected, window_fec_scheme_specific_t fss) {
    wff->repair_symbols_queue[idx].repair_symbol = rs;
    wff->repair_symbols_queue[idx].n_protected_symbols = n_protected_symbols;
    wff->repair_symbols_queue[idx].first_protected_symbol = first_protected;
    wff->repair_symbols_queue[idx].fss = fss;
}

// adds a repair symbol in the queue waiting for the symbol to be sent
static __attribute__((always_inline)) void queue_repair_symbol(picoquic_cnx_t *cnx, window_fec_framework_t *wff, repair_symbol_t *rs,
        uint16_t n_protected_symbols, source_symbol_id_t first_protected_symbol, window_fec_scheme_specific_t fss){
    int idx = ((uint32_t) ((uint32_t) wff->repair_symbols_queue_head + wff->repair_symbols_queue_length)) % MAX_QUEUED_REPAIR_SYMBOLS;
    if (has_repair_symbol_at_index(wff, idx)) {
        remove_item_at_index(cnx, wff, idx);
        if (wff->repair_symbols_queue_length > 1 && wff->repair_symbols_queue_head == idx) {
            // the head is the next symbol
            wff->repair_symbols_queue_head = ((uint32_t) ( (uint32_t) wff->repair_symbols_queue_head + 1)) % MAX_QUEUED_REPAIR_SYMBOLS;
        }
        wff->repair_symbols_queue_length--;
    }
    put_item_at_index(wff, idx, rs, n_protected_symbols, first_protected_symbol, fss);
    if (wff->repair_symbols_queue_length == 0) {
        wff->repair_symbols_queue_head = idx;
    }
    wff->repair_symbols_queue_length++;
}

// peeks a repair symbol from the queue
static __attribute__((always_inline)) repair_symbol_t *peek_repair_symbol(picoquic_cnx_t *cnx, window_fec_framework_t *wff,
        uint16_t *n_protected_symbols, window_source_symbol_id_t *first_protected_symbol, window_fec_scheme_specific_t *fss){
    int idx = wff->repair_symbols_queue_head;
    if (has_repair_symbol_at_index(wff, idx)) {
        queue_item ret = wff->repair_symbols_queue[idx];
        *n_protected_symbols = ret.n_protected_symbols;
        *first_protected_symbol = ret.first_protected_symbol;
        *fss = ret.fss;
        return ret.repair_symbol;
    }
    return NULL;
}

// removes a repair symbol from the queue
static __attribute__((always_inline)) repair_symbol_t *dequeue_repair_symbol(picoquic_cnx_t *cnx, window_fec_framework_t *wff,
        uint16_t *n_protected_symbols, window_source_symbol_id_t *first_protected_symbol, window_fec_scheme_specific_t *fss){
    repair_symbol_t *ret = peek_repair_symbol(cnx, wff, n_protected_symbols, first_protected_symbol, fss);
    if (ret) {
        // there is a symbol, so remove it
        remove_item_at_index(cnx, wff, wff->repair_symbols_queue_head);
        if (wff->repair_symbols_queue_length > 1) {
            // the head is the next symbol
            wff->repair_symbols_queue_head = (wff->repair_symbols_queue_head + 1U) % MAX_QUEUED_REPAIR_SYMBOLS;
        }
        wff->repair_symbols_queue_length--;
    }
    return ret;
}

// adds a repair symbol in the queue waiting for the symbol to be sent
static __attribute__((always_inline)) void queue_repair_symbols(picoquic_cnx_t *cnx, window_fec_framework_t *wff, repair_symbol_t *rss[], uint16_t n_repair_symbols,
        uint16_t n_protected_symbols, window_source_symbol_id_t first_protected_symbol, window_fec_scheme_specific_t fss){
    for (int i = 0 ; i < n_repair_symbols ; i++) {
        queue_repair_symbol(cnx, wff, rss[i], n_protected_symbols, first_protected_symbol, fss);
    }
}

static __attribute__((always_inline)) int get_repair_symbols_from_queue(picoquic_cnx_t *cnx, window_fec_framework_t *wff, uint16_t symbol_size, repair_symbol_t **symbols, size_t bytes_max,
        uint8_t symbols_max, size_t *consumed, uint8_t *added_symbols, uint16_t *n_protected_symbols, window_source_symbol_id_t *first_protected_symbol,
        window_fec_scheme_specific_t *fss){
    if (wff->repair_symbols_queue_length == 0)
        return 0;
    // FIXME: temporarily ensure that the repair symbols are not split into multiple frames
    if (bytes_max < REPAIR_FRAME_HEADER_SIZE + symbol_size) {
        PROTOOP_PRINTF(cnx, "NOT ENOUGH BYTES TO SEND SYMBOL: %u < %u\n", bytes_max, REPAIR_FRAME_HEADER_SIZE + symbol_size);
        return PICOQUIC_ERROR_SEND_BUFFER_TOO_SMALL;
    }

    *added_symbols = 0;
    *consumed = REPAIR_FRAME_HEADER_SIZE;

    while (*consumed + symbol_size <= bytes_max && *added_symbols < symbols_max) {
        uint16_t current_protected_symbols = 0;
        window_source_symbol_id_t current_first_protected;
        repair_symbol_t *rs = peek_repair_symbol(cnx, wff, &current_protected_symbols, &current_first_protected, fss);
        if (*added_symbols == 0) {
            *n_protected_symbols = current_protected_symbols;
            *first_protected_symbol = current_first_protected;
        } else if (current_protected_symbols != *n_protected_symbols || current_first_protected != *first_protected_symbol) {
            // we bulk-enqueue repair symbols that protect exactly the same source symbols
            break;
        }

        symbols[*added_symbols] = rs;
        *added_symbols += 1;
        *consumed += symbol_size;
        dequeue_repair_symbol(cnx, wff, &current_protected_symbols, &current_first_protected, fss);
    }
    return 0;
}

static __attribute__((always_inline)) int get_repair_frame_to_send(picoquic_cnx_t *cnx, window_fec_framework_t *wff, uint16_t symbol_size, size_t bytes_max,
                                                                    size_t *predicted_size, window_repair_frame_t *rf) {
    uint8_t symbols_max = 100;
    uint8_t added_symbols = 0;
    repair_symbol_t **symbols = my_malloc(cnx, symbols_max*sizeof(repair_symbol_t *));
    if (!symbols)
        return PICOQUIC_ERROR_MEMORY;
    rf->symbols = symbols;
    my_memset(&rf, 0, sizeof(rf));
    int err = get_repair_symbols_from_queue(cnx, wff, symbol_size, rf->symbols, bytes_max, symbols_max, predicted_size, &added_symbols,
            &rf->n_protected_symbols, &rf->first_protected_symbol, &rf->fss);
    if (err) {
        my_free(cnx, symbols);
        return err;
    }
    rf->n_repair_symbols = added_symbols;
//    err = serialize_window_repair_frame(cnx, buffer, bytes_max, &rf, symbol_size, predicted_size);
//    my_free(cnx, symbols);
    return err;
}


static __attribute__((always_inline)) int reserve_repair_frames(picoquic_cnx_t *cnx, window_fec_framework_t *wff, size_t size_max, size_t symbol_size) {
    if (size_max < REPAIR_FRAME_HEADER_SIZE + symbol_size) {
        PROTOOP_PRINTF(cnx, "NOT ENOUGH SPACE TO RESERVE A REPAIR FRAME\n");
        return -1;
    }
    while (wff->repair_symbols_queue_length != 0 && size_max >= REPAIR_FRAME_HEADER_SIZE + symbol_size) {
        window_repair_frame_t *rf = create_repair_frame(cnx);
        if (!rf) {
            return PICOQUIC_ERROR_MEMORY;
        }
        my_memset(rf, 0, sizeof(window_repair_frame_t));
        size_t predicted_size = 0;
        int err = get_repair_frame_to_send(cnx, wff, symbol_size, size_max - REPAIR_FRAME_TYPE_BYTE_SIZE,
                                           &predicted_size, rf);
        if (err) {
            my_free(cnx, rf);
            return err;
        }

        reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
        if (!slot) {
            my_free(cnx, rf);
            return PICOQUIC_ERROR_MEMORY;
        }
        my_memset(slot, 0, sizeof(reserve_frame_slot_t));
        slot->frame_type = FRAME_REPAIR;
        slot->nb_bytes = REPAIR_FRAME_TYPE_BYTE_SIZE + predicted_size;
        slot->frame_ctx = rf;
        slot->is_congestion_controlled = true;
        PROTOOP_PRINTF(cnx, "RESERVE REPAIR FRAMES\n");
        size_t reserved_size = reserve_frames(cnx, 1, slot);
        if (reserved_size < slot->nb_bytes) {
            PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
            delete_repair_frame(cnx, rf);
            my_free(cnx, slot);
            return 1;
        }
    }

    return 0;
}


static __attribute__((always_inline)) bool _remove_source_symbol_from_window(picoquic_cnx_t *cnx, window_fec_framework_t *wff, source_symbol_t *ss, window_source_symbol_id_t id){

    int idx = (int) (id % MAX_SENDING_WINDOW_SIZE);
    // wrong symbol ?
    if (!wff->fec_window[idx].symbol || wff->fec_window[idx].id != id)
        return false;
    if (wff->fec_window[idx].id == wff->min_id) wff->min_id++;
    delete_source_symbol(cnx, wff->fec_window[idx].symbol);
    wff->fec_window[idx].symbol = NULL;
    wff->fec_window[idx].id = 0;
    wff->fec_window[idx].received = false;
    // one less symbol
    wff->window_length--;
    return true;
}

static __attribute__((always_inline)) bool remove_source_symbol_from_window(picoquic_cnx_t *cnx, window_fec_framework_t *wff, source_symbol_t *ss, window_source_symbol_id_t id) {
    if (ss) {
        if (id != wff->smallest_in_transit || is_fec_window_empty(wff)) {
            return false;
        }

        if (!_remove_source_symbol_from_window(cnx, wff, ss, id))
            return false;
        wff->smallest_in_transit++;

        while(!is_fec_window_empty(wff) && wff->fec_window[wff->smallest_in_transit % MAX_SENDING_WINDOW_SIZE].received) {

            if (!_remove_source_symbol_from_window(cnx, wff, wff->fec_window[wff->smallest_in_transit % MAX_SENDING_WINDOW_SIZE].symbol, id))
                return false;
            wff->smallest_in_transit++;
        }

        if (is_fec_window_empty(wff)) {
            wff->smallest_in_transit = wff->highest_in_transit = INITIAL_SYMBOL_ID-1;
        }
        return true;
    }
    return false;
}

static __attribute__((always_inline)) bool add_source_symbol_to_window(picoquic_cnx_t *cnx, window_fec_framework_t *wff, source_symbol_t *ss, window_source_symbol_id_t id) {
    if (ss) {
        int idx = (int) (id % MAX_SENDING_WINDOW_SIZE);
        if (wff->fec_window[idx].symbol || id <= wff->highest_in_transit) {
            // we cannot add a symbol if another one is already there
            return false;
        }
        wff->fec_window[idx].symbol = ss;
        wff->fec_window[idx].received = false;
        wff->fec_window[idx].id = id;
        if (wff->window_length == 0) {
            wff->min_id = wff->max_id = id;
            wff->highest_in_transit = id;
            wff->smallest_in_transit = id;
        }
        // one more symbol
        wff->window_length++;
        wff->highest_in_transit = id;
        return true;
    }
    return false;

}

static __attribute__((always_inline)) int sfpid_has_landed(picoquic_cnx_t *cnx, window_fec_framework_t *wff, window_source_symbol_id_t id, bool received) {
    PROTOOP_PRINTF(cnx, "SYMBOL %d LANDED, RECEIVED = %d\n", id, received);
    // remove all the needed symbols from the window
    uint32_t idx = id % MAX_SENDING_WINDOW_SIZE;
    if (received && wff->fec_window[idx].symbol) {
        if (wff->fec_window[idx].id == id) {
            wff->fec_window[idx].received = true;
            // if it is the first symbol of the window, let's prune the window
            if (!is_fec_window_empty(wff) && id == wff->smallest_in_transit) {
                if (!remove_source_symbol_from_window(cnx, wff, wff->fec_window[idx].symbol, id)) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static __attribute__((always_inline)) void sfpid_takes_off(window_fec_framework_t *wff, window_source_symbol_id_t id) {
    wff->highest_in_transit = MAX(wff->highest_in_transit, id);
    wff->smallest_in_transit = MIN(wff->smallest_in_transit, id);
}



static __attribute__((always_inline)) void remove_source_symbols_from_window(picoquic_cnx_t *cnx, window_fec_framework_t *wff, source_symbol_t **symbols, uint16_t n_symbols, window_source_symbol_id_t first_symbol_id){
    for (int i = 0 ; i < n_symbols ; i++) {
        source_symbol_t *ss = symbols[i];
        remove_source_symbol_from_window(cnx, wff, ss, first_symbol_id + i);
        symbols[i] = NULL;
    }
}

static __attribute__((always_inline)) int generate_and_queue_repair_symbols(picoquic_cnx_t *cnx, window_fec_framework_t *wff, bool flush,
                                                                            uint16_t n_symbols_to_generate, uint16_t symbol_size){
    protoop_arg_t args[6];
    protoop_arg_t outs[2];

    // build the block to generate the symbols


    source_symbol_t **symbols = my_malloc(cnx, wff->window_length*sizeof(source_symbol_t *));
    if (!symbols)
        return PICOQUIC_ERROR_MEMORY;
    my_memset(symbols, 0, wff->window_length*sizeof(source_symbol_t *));
    args[0] = (protoop_arg_t) symbols;
    args[1] = (protoop_arg_t) wff;
    args[2] = flush;

    int ret = 0;
    ret = (int) run_noparam(cnx, "window_select_symbols_to_protect", 3, args, outs);
    if (ret) {
        PROTOOP_PRINTF(cnx, "ERROR WHEN SELECTING THE SYMBOLS TO PROTECT\n");
        return ret;
    }
    uint16_t n_symbols = outs[0];
    source_symbol_id_t first_protected_id = outs[1];
    if (n_symbols > 0) {
        repair_symbol_t **repair_symbols = my_malloc(cnx, n_symbols*sizeof(repair_symbol_t *));
        if (!repair_symbols) {
            my_free(cnx, symbols);
            return PICOQUIC_ERROR_MEMORY;
        }
        my_memset(repair_symbols, 0, n_symbols*sizeof(repair_symbol_t *));
        args[0] = (protoop_arg_t) wff->fec_scheme;
        args[1] = (protoop_arg_t) symbols;
        args[2] = (protoop_arg_t) n_symbols;
        args[3] = (protoop_arg_t) repair_symbols;
        args[4] = (protoop_arg_t) n_symbols_to_generate;
        args[5] = (protoop_arg_t) symbol_size;
        ret = (int) run_noparam(cnx, "fec_generate_repair_symbols", 6, args, outs);
        window_fec_scheme_specific_t first_fec_scheme_specific;
        first_fec_scheme_specific.val_big_endian = (uint32_t) outs[0];
        uint16_t n_symbols_generated = outs[1];
        if (!ret) {
            queue_repair_symbols(cnx, wff, repair_symbols, n_symbols_generated, n_symbols, first_protected_id, first_fec_scheme_specific);
//            uint32_t last_id = fb->source_symbols[fb->total_source_symbols-1]->source_fec_payload_id.raw;
            // we don't free the source symbols: they can still be used afterwards
            // we don't free the repair symbols, they are queued and will be free afterwards
            // so we only free the fec block
//            wff->highest_sent_id = MAX(last_id, wff->highest_sent_id);
//            reserve_repair_frames(cnx, wff, PICOQUIC_MAX_PACKET_SIZE, symbol_size);
        }
        my_free(cnx, repair_symbols);
    } else {
        PROTOOP_PRINTF(cnx, "NO SYMBOL TO PROTECT\n");
    }
    my_free(cnx, symbols);

    return ret;
}


static __attribute__((always_inline)) window_source_symbol_id_t window_get_next_source_symbol_id(window_fec_framework_t *wff){
    return wff->max_id + 1;
}

static __attribute__((always_inline)) int select_all_inflight_source_symbols(picoquic_cnx_t *cnx, window_fec_framework_t *wff,
                                                source_symbol_t **symbols, uint16_t *n_symbols, window_source_symbol_id_t *first_protected_id)
{
    uint16_t n = 0;
    for (int i = MAX(wff->smallest_in_transit, wff->highest_in_transit - MIN(MAX_SENDING_WINDOW_SIZE, wff->highest_in_transit)) ; i <= wff->highest_in_transit ; i++) {
        uint32_t idx = ((uint32_t) i) % MAX_SENDING_WINDOW_SIZE;
        source_symbol_t *ss = wff->fec_window[idx].symbol;
        if (!ss || wff->fec_window[idx].id != i)
            return -1;
        if (n == 0)
            *first_protected_id = wff->fec_window[idx].id;
        symbols[n++] = ss;
    }
    *n_symbols = n;

    return 0;
}

static __attribute__((always_inline)) int get_one_causal_packet(picoquic_cnx_t *cnx, window_fec_framework_t *wff,
                                            uint8_t *nss, causal_packet_type_t type, uint16_t symbol_size, uint16_t packet_size){
    if (wff->window_length == 0)
        return 0;

    protoop_arg_t args[7];
    protoop_arg_t outs[2];

    source_symbol_t **symbols = my_malloc(cnx, wff->window_length*sizeof(source_symbol_t *));
    if (!symbols)
        return PICOQUIC_ERROR_MEMORY;
    my_memset(symbols, 0, wff->window_length*sizeof(source_symbol_t *));
    uint16_t n_symbols_to_generate = packet_size / symbol_size + 1;
    repair_symbol_t **repair_symbols = my_malloc(cnx, n_symbols_to_generate*sizeof(repair_symbol_t *));
    if (!repair_symbols) {
        my_free(cnx, symbols);
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(repair_symbols, 0, n_symbols_to_generate*sizeof(repair_symbol_t *));
    uint16_t n_selected_symbols = 0;
    window_source_symbol_id_t first_protected_id = 0;
    if (select_all_inflight_source_symbols(cnx, wff, symbols, &n_selected_symbols, &first_protected_id)) {
        my_free(cnx, symbols);
        return PICOQUIC_ERROR_MEMORY;
    }

    args[0] = (protoop_arg_t) wff->fec_scheme;
    args[1] = (protoop_arg_t) symbols;
    args[2] = (protoop_arg_t) n_selected_symbols;
    args[3] = (protoop_arg_t) repair_symbols;
    args[4] = (protoop_arg_t) n_symbols_to_generate;
    args[5] = (protoop_arg_t) wff->current_slot;
    args[6] = (protoop_arg_t) symbol_size;

    int err = run_noparam(cnx, "rlc_gf256_get_one_coded_symbol", 7, args, outs);
    window_fec_scheme_specific_t first_fec_scheme_specific;
    first_fec_scheme_specific.val_big_endian = (uint32_t) outs[0];
    uint16_t n_symbols_generated = outs[1];
    if (n_symbols_generated != n_symbols_to_generate) {
        my_free(cnx, symbols);
        my_free(cnx, repair_symbols);
        return PICOQUIC_ERROR_MEMORY;
    }
    if (!err) {
        queue_repair_symbols(cnx, wff, repair_symbols, n_symbols_generated, n_selected_symbols, first_protected_id, first_fec_scheme_specific);
//            uint32_t last_id = fb->source_symbols[fb->total_source_symbols-1]->source_fec_payload_id.raw;
        // we don't free the source symbols: they can still be used afterwards
        // we don't free the repair symbols, they are queued and will be free afterwards
        // so we only free the fec block
//            wff->highest_sent_id = MAX(last_id, wff->highest_sent_id);
//            reserve_repair_frames(cnx, wff, PICOQUIC_MAX_PACKET_SIZE, symbol_size);
    }

    my_free(cnx, symbols);
    my_free(cnx, repair_symbols);
    return 0;
}

static __attribute__((always_inline)) int protect_source_symbol(picoquic_cnx_t *cnx, window_fec_framework_t *wff, source_symbol_t *ss, window_source_symbol_id_t *id) {
    *id = ++wff->max_id;
    if (!add_source_symbol_to_window(cnx, wff, ss, *id)) {
        PROTOOP_PRINTF(cnx, "COULDN't ADD\n");
        return -1;
    }

    wff->highest_in_transit = *id;

    return 0;
}


static __attribute__((always_inline)) uint64_t window_sent_symbol(picoquic_cnx_t *cnx, window_fec_framework_t *wff, causal_packet_type_t type, rlnc_window_t *window) {
    sent_packet(wff->controller, type, wff->current_slot, window);
    return wff->current_slot++;
}

static __attribute__((always_inline)) rlnc_window_t get_current_rlnc_window(picoquic_cnx_t *cnx, window_fec_framework_t *wff) {
    rlnc_window_t window;
    window.start = 0;
    window.end = 0;
    if (!is_fec_window_empty(wff)) {
        window.start = wff->smallest_in_transit;
        window.end = wff->highest_in_transit + 1;
    }
    return window;
}


static __attribute__((always_inline)) void window_slot_acked(picoquic_cnx_t *cnx, window_fec_framework_t *wff, uint64_t slot) {
    rlnc_window_t window = get_current_rlnc_window(cnx, wff);
    slot_acked(cnx, wff->controller, slot, &window);
}

static __attribute__((always_inline)) void window_free_slot_without_feedback(picoquic_cnx_t *cnx, window_fec_framework_t *wff) {
    rlnc_window_t window = get_current_rlnc_window(cnx, wff);
    free_slot_without_feedback(cnx, wff->controller, &window);
}

static __attribute__((always_inline)) void window_slot_nacked(picoquic_cnx_t *cnx, window_fec_framework_t *wff, uint64_t slot) {
    rlnc_window_t window = get_current_rlnc_window(cnx, wff);
    slot_nacked(cnx, wff->controller, slot, &window);
}

static __attribute__((always_inline)) causal_packet_type_t window_what_to_send(picoquic_cnx_t *cnx, window_fec_framework_t *wff) {
    if (wff->window_length == MAX_SLOTS)
        return fec_packet;
    causal_packet_type_t what = what_to_send(cnx, wff->controller);
    if (what == nothing) {
        window_free_slot_without_feedback(cnx, wff);
        what = what_to_send(cnx, wff->controller);
        if (what == nothing)
            return new_rlnc_packet;
    }
    // if the window is empty, we cannot send a FEC packet, so just send a new packet
    return is_fec_window_empty(wff) ? new_rlnc_packet : what;
}

static __attribute__((always_inline)) void window_packet_has_been_recovered(picoquic_cnx_t *cnx, window_fec_framework_t *wff, uint64_t pn, window_source_symbol_id_t first_id) {
    enqueue_recovered_packet_to_buffer(wff->rps, pn);
}

static __attribute__((always_inline)) void process_recovered_packets(picoquic_cnx_t *cnx, window_fec_framework_t *wff, const uint8_t *size_and_packets) {
    uint64_t n_packets = *((uint64_t *) size_and_packets);
    uint64_t *packet_numbers = (uint64_t *) (size_and_packets + sizeof(uint64_t));
    window_source_symbol_id_t *ids = (window_source_symbol_id_t  *) (packet_numbers + n_packets);
    for (int i = 0 ; i < n_packets ; i++) {
        window_packet_has_been_recovered(cnx, wff, packet_numbers[i], ids[i]);
    }
}


static __attribute__((always_inline)) int window_detect_lost_protected_packets(picoquic_cnx_t *cnx, window_fec_framework_t *wff, uint64_t current_time, picoquic_packet_context_enum pc) {

    char *reason = NULL;
    int nb_paths = (int) get_cnx(cnx, AK_CNX_NB_PATHS, 0);
    bool stop = false;
    for (int i = 0; i < nb_paths; i++) {
        picoquic_path_t *orig_path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, i);
        picoquic_packet_context_t *orig_pkt_ctx = (picoquic_packet_context_t *) get_path(orig_path, AK_PATH_PKT_CTX,
                                                                                         pc);
        picoquic_packet_t *p = (picoquic_packet_t *) get_pkt_ctx(orig_pkt_ctx, AK_PKTCTX_RETRANSMIT_OLDEST);
        /* TODO: while packets are pure ACK, drop them from retransmit queue */
        while (p != NULL) {
            int should_retransmit = 0;
            int timer_based_retransmit = 0;
            uint64_t lost_packet_number = (uint64_t) get_pkt(p, AK_PKT_SEQUENCE_NUMBER);
            picoquic_packet_t *p_next = (picoquic_packet_t *) get_pkt(p, AK_PKT_NEXT_PACKET);
            picoquic_packet_type_enum ptype = (picoquic_packet_type_enum) get_pkt(p, AK_PKT_TYPE);

            /* Get the packet type */

            should_retransmit = helper_retransmit_needed_by_packet(cnx, p, current_time, &timer_based_retransmit,
                                                                   &reason);

            if (should_retransmit == 0) {
                /*
                * Always retransmit in order. If not this one, then nothing.
                * But make an exception for 0-RTT packets.
                */
                if (ptype == picoquic_packet_0rtt_protected) {
                    p = p_next;
                    continue;
                } else {
                    stop = true;
                    break;
                }
            } else {
                /* check if this is an ACK only packet */
//                int contains_crypto = (int) get_pkt(p, AK_PKT_CONTAINS_CRYPTO);
                int packet_is_pure_ack = (int) get_pkt(p, AK_PKT_IS_PURE_ACK);
                int do_not_detect_spurious = 1;
                /* TODO: should be the path on which the packet was transmitted */
                picoquic_path_t *old_path = (picoquic_path_t *) get_pkt(p, AK_PKT_SEND_PATH);

                if (should_retransmit != 0) {

                    /* Update the number of bytes in transit and remove old packet from queue */
                    /* If not pure ack, the packet will be placed in the "retransmitted" queue,
                    * in order to enable detection of spurious restransmissions */
                    // if the pc is the application, we want to free the packet: indeed, we disable the retransmissions

                    uint64_t slot = get_pkt_metadata(cnx, p, FEC_PKT_METADATA_SENT_SLOT);
                    bool fec_protected = get_pkt_metadata(cnx, p, FEC_PKT_METADATA_IS_FEC_PROTECTED);
                    bool fec_related = fec_protected || get_pkt_metadata(cnx, p, FEC_PKT_METADATA_CONTAINS_FEC_PACKET);
                    uint32_t sfpid = get_pkt_metadata(cnx, p, FEC_PKT_METADATA_SFPID);
                    plugin_state_t *state = NULL;
                    if (fec_related) {
                        state = get_plugin_state(cnx);
                        if (!state)
                            return PICOQUIC_ERROR_MEMORY;
                        state->current_packet_is_lost = true;
                        int err = 0;
                        if (fec_protected &&
                            (err = add_lost_packet(cnx, &state->lost_packets, lost_packet_number, slot, sfpid))) {
                            return err;
                        }
                        helper_dequeue_retransmit_packet(cnx, p, (packet_is_pure_ack & do_not_detect_spurious) ||
                                                                 (pc == picoquic_packet_context_application &&
                                                                  !get_pkt(p, AK_PKT_IS_MTU_PROBE)));

                        state->current_packet_is_lost = false;
                        window_slot_nacked(cnx, wff, slot);
                        helper_congestion_algorithm_notify(cnx, old_path,
                                                           (timer_based_retransmit == 0)
                                                           ? picoquic_congestion_notification_repeat
                                                           : picoquic_congestion_notification_timeout,
                                                           0, 0, lost_packet_number, current_time);
                        stop = true;
                        break;
                    }
                }
                /*
                * If the loop is continuing, this means that we need to look
                * at the next candidate packet.
                */
                p = p_next;
            }

            if (stop) {
                break;
            }
        }
    }
    return 0;
}



static __attribute__((always_inline)) void window_maybe_notify_recovered_packets_to_cc(picoquic_cnx_t *cnx, recovered_packets_buffer_t *b, uint64_t current_time) {
    // TODO: handle multipath, currently only single-path
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    picoquic_packet_context_t *pkt_ctx = (picoquic_packet_context_t *) get_path(path, AK_PATH_PKT_CTX, picoquic_packet_context_application);
    picoquic_packet_t *current_packet = (picoquic_packet_t *) get_pkt_ctx(pkt_ctx, AK_PKTCTX_RETRANSMIT_OLDEST);
    while(b->size > 0 && current_packet) {
        picoquic_packet_t *pnext = (picoquic_packet_t *) get_pkt(current_packet, AK_PKT_NEXT_PACKET);
        uint64_t current_pn64 = get_pkt(current_packet, AK_PKT_SEQUENCE_NUMBER);
        if (current_pn64 == peek_first_recovered_packet_in_buffer(b)) {
            int timer_based = 0;
            if (!helper_retransmit_needed_by_packet(cnx, current_packet, current_time, &timer_based, NULL)) {
                // we don't need to notify it now: the packet is not considered as lost
                // don't try any subsequenc packets as they have been sent later
                break;
            }
            //we need to remove this packet from the retransmit queue
            uint64_t retrans_cc_notification_timer = get_pkt_ctx(pkt_ctx, AK_PKTCTX_LATEST_RETRANSMIT_CC_NOTIFICATION_TIME) + get_path(path, AK_PATH_SMOOTHED_RTT, 0);
            bool packet_is_pure_ack = get_pkt(current_packet, AK_PKT_IS_PURE_ACK);
            // notify everybody that this packet is lost
            helper_packet_was_lost(cnx, current_packet, path);
            helper_dequeue_retransmit_packet(cnx, current_packet, 1);
            if (current_time >= retrans_cc_notification_timer && !packet_is_pure_ack) {    // do as in core: if is pure_ack or recently notified, do not notify cc
                set_pkt_ctx(pkt_ctx, AK_PKTCTX_LATEST_RETRANSMIT_CC_NOTIFICATION_TIME, current_time);
                helper_congestion_algorithm_notify(cnx, path, picoquic_congestion_notification_repeat, 0, 0,
                                                   current_pn64, current_time);
            }
            PROTOOP_PRINTF(cnx, "[[PACKET RECOVERED]] %lu,%lu\n", current_pn64, current_time - get_pkt(current_packet, AK_PKT_SEND_TIME));
            dequeue_recovered_packet_from_buffer(b);
        } else if (current_pn64 > peek_first_recovered_packet_in_buffer(b)) {
            // the packet to remove is already gone from the retransmit queue
            dequeue_recovered_packet_from_buffer(b);
        } // else, do nothing, try the next packet
        current_packet = pnext;
    }
}




//#endif //PICOQUIC_FEC_WINDOW_FRAMEWORK_SENDER_H
