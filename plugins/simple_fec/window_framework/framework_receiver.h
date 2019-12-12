#include <zlib.h>
#include "../../helpers.h"
#include "window_receive_buffers.h"
#include "types.h"

#define MAX_RECEIVE_BUFFER_SIZE 450


#define MIN(a, b) ((a < b) ? a : b)


#define MAX_AMBIGUOUS_ID_GAP ((uint32_t) 0x200*2)       // the max ambiguous ID gap depends on the max number of source symbols that can be protected by a Repair Symbol


typedef struct {
    // those two booleans are used to avoid a call-cycle in the protoops
    // when we receive a symbol, we mark the event and we try to recover new packets
    // afterwards
    bool has_received_a_source_symbol;
    bool has_received_a_repair_symbol;

    uint32_t highest_removed;
    fec_scheme_t fs;
    received_symbols_tracker_t symbols_tracker;
    received_source_symbols_buffer_t *received_source_symbols;
    received_repair_symbols_buffer_t *received_repair_symbols;

    window_repair_symbol_t **rs_recovery_buffer;
    window_source_symbol_t **ss_recovery_buffer;
    window_source_symbol_id_t *missing_symbols_buffer;

    min_max_pq_t recovered_packets;
    
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
    wff->symbols_tracker = create_received_symbols_tracker(cnx, WINDOW_INITIAL_SYMBOL_ID);
    if (!wff->symbols_tracker) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        my_free(cnx, wff->ss_recovery_buffer);
        my_free(cnx, wff->rs_recovery_buffer);
        my_free(cnx, wff->missing_symbols_buffer);
        my_free(cnx, wff);
        return NULL;
    }
    wff->recovered_packets = create_min_max_pq(cnx, MAX_RECEIVE_BUFFER_SIZE);
    if (!wff->symbols_tracker) {
        release_source_symbols_buffer(cnx, wff->received_source_symbols);
        release_repair_symbols_buffer(cnx, wff->received_repair_symbols);
        delete_received_symbols_tracker(cnx, wff->symbols_tracker);
        my_free(cnx, wff->ss_recovery_buffer);
        my_free(cnx, wff->rs_recovery_buffer);
        my_free(cnx, wff->missing_symbols_buffer);
        my_free(cnx, wff);
        return NULL;
    }
    return wff;
}

// pre: buffers sizes == MAX_RECEIVE_BUFFER_SIZE
static __attribute__((always_inline)) void select_source_and_repair_symbols(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t **source_symbols,
                                                                            uint16_t *n_considered_source_symbols, window_source_symbol_id_t *first_symbol_id,
                                                                            window_repair_symbol_t **repair_symbols, uint16_t *n_repair_symbols,
                                                                            uint16_t *n_missing_source_symbols,
                                                                            window_source_symbol_id_t *missing_source_symbols
                                                                            ) {
    window_source_symbol_id_t first_id = 0, last_id = 0;
    *n_repair_symbols = get_repair_symbols(cnx, wff->received_repair_symbols, repair_symbols, &first_id, &last_id);
    *n_considered_source_symbols = last_id + 1 - first_id;
    if (*n_repair_symbols > 0) {
        uint16_t n_added_source_symbols = get_source_symbols_between_bounds(cnx, wff->received_source_symbols, source_symbols, first_id, last_id);
        *first_symbol_id = first_id;
        PROTOOP_PRINTF(cnx, "FIRST ID = %u, LAST ID = %u\n", first_id, last_id);
        *n_missing_source_symbols = *n_considered_source_symbols - n_added_source_symbols;
        int current_missing = 0;
        for (int i = 0 ; i < last_id + 1 - first_id ; i++) {
            if (!source_symbols[i]) {
                missing_source_symbols[current_missing++] = first_id + i;
            }
        }
        if (current_missing == 0) {
            // there is no missing symbol protected by our currently received repair symbol !
            // no recovery is possible then
            *n_missing_source_symbols = 0;
        }
    } else {
        *n_considered_source_symbols = 0;
        *n_missing_source_symbols = 0;
    }
}


static __attribute__((always_inline)) int recover_lost_symbols(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t **source_symbols,
            uint16_t n_source_symbols, window_source_symbol_id_t first_symbol_id, window_repair_symbol_t **repair_symbols, uint16_t n_repair_symbols,
            uint16_t n_missing_source_symbols, window_source_symbol_id_t *missing_symbols_buffer, uint16_t symbol_size) {
    protoop_arg_t args[9];
    args[0] = (protoop_arg_t) wff->fs;
    args[1] = (protoop_arg_t) source_symbols;
    args[2] = (protoop_arg_t) n_source_symbols;
    args[3] = (protoop_arg_t) repair_symbols;
    args[4] = (protoop_arg_t) n_repair_symbols;
    args[5] = (protoop_arg_t) n_missing_source_symbols;
    args[6] = (protoop_arg_t) missing_symbols_buffer;
    args[7] = (protoop_arg_t) first_symbol_id;
    args[8] = (protoop_arg_t) symbol_size;

    return (int) run_noparam(cnx, FEC_PROTOOP_WINDOW_FEC_SCHEME_RECOVER, 9, args, NULL);
}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int window_receive_source_symbol(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t *ss){
    window_source_symbol_id_t removed_id = 0;
    window_source_symbol_t *removed = add_source_symbol(cnx, wff->received_source_symbols, ss);
    PROTOOP_PRINTF(cnx, "ADD SOURCE SYMBOL %u\n", ss->id);
    if (removed) {
        wff->highest_removed = MAX(removed_id, wff->highest_removed);
        PROTOOP_PRINTF(cnx, "REMOVED SOURCE SYMBOL %u (whole data = %p) TO ADD SYMBOL %u (whole data = %p)\n", removed->id, (protoop_arg_t) removed->source_symbol._whole_data, ss->id, (protoop_arg_t) ss->source_symbol._whole_data);
        delete_window_source_symbol(cnx, removed);
    }
    uint32_t highest_contiguous_received = get_highest_contiguous_received_source_symbol(wff->symbols_tracker);
    PROTOOP_PRINTF(cnx, "HIGHEST_CONTIGUOUSLY_RECEIVED = %u\n", highest_contiguous_received);
    int err = tracker_receive_source_symbol(cnx, wff->symbols_tracker, ss->id);
    if (err) {
        return err;
    }
    wff->has_received_a_source_symbol = true;
    if (highest_contiguous_received != get_highest_contiguous_received_source_symbol(wff->symbols_tracker)) {
        // let's find all the blocks protecting this symbol to see if we can recover the remaining
        // we don't recover symbols if we already are in recovery mode
        remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, get_highest_contiguous_received_source_symbol(wff->symbols_tracker) + 1);
    }
    return 0;
}


static __attribute__((always_inline)) int window_receive_packet_payload(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff,
        uint8_t *payload, size_t payload_length, uint64_t packet_number, window_source_symbol_id_t first_symbol_id, size_t symbol_size) {
    uint16_t n_chunks = 0;
    source_symbol_t **sss = packet_payload_to_source_symbols(cnx, payload, payload_length, symbol_size, packet_number, &n_chunks, sizeof(window_source_symbol_t));
    if (!sss)
        return PICOQUIC_ERROR_MEMORY;

    for (int i = 0 ; i < n_chunks ; i++) {
        window_source_symbol_t *ss = (window_source_symbol_t *) sss[i];
        ss->id = first_symbol_id + i;
        int err = window_receive_source_symbol(cnx, wff, ss);
        if (err) {
            my_free(cnx, sss);
            return err;
        }
    }
    my_free(cnx, sss);
    return 0;
}


// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int window_receive_repair_symbol(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_repair_symbol_t *rs) {
    if (rs->metadata.first_id + rs->metadata.n_protected_symbols - 1 <= get_highest_contiguous_received_source_symbol(wff->symbols_tracker))
        return false;
    repair_symbol_t *removed = add_repair_symbol(cnx, wff->received_repair_symbols, rs);
    if (removed)
        delete_repair_symbol(cnx, removed);
    wff->has_received_a_repair_symbol = true;
    return true;
}

//pre: the rss must have been created as an array of (window_repair_symbol_t *)
static __attribute__((always_inline)) void window_receive_repair_symbols(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, repair_symbol_t **rss, uint16_t n_symbols) {
    for (int i = 0 ; i < n_symbols ; i++) {
        if (!window_receive_repair_symbol(cnx, wff, (window_repair_symbol_t *) rss[i])) {
            delete_repair_symbol(cnx, rss[i]);
            rss[i] = NULL;
        }
    }
}


// returns true if it succeeded, false otherwise
static __attribute__((always_inline)) bool reassemble_packet_from_recovered_symbol(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, uint8_t *buffer,
        size_t buffer_size, window_source_symbol_t **source_symbols, uint16_t n_source_symbols, uint32_t recovered_symbol_idx,
        size_t *payload_size, uint64_t *packet_number, window_source_symbol_id_t *first_id_in_packet) {
    // first, find the start of the packet backwards
    int start_index = -1;
    PROTOOP_PRINTF(cnx, "TRY TO REASSEMBLE\n");
    for (int current_index = recovered_symbol_idx ; current_index >= 0; current_index--) {
        PROTOOP_PRINTF(cnx, "CONSIDER SYMBOL AT INDEX %d\n", current_index);
        if (!source_symbols[current_index]) {
            // the packet cannot be complete, so we failed reassembling it
            return false;
        }
        if (get_ss_metadata_S(&source_symbols[current_index]->source_symbol)) {
            start_index = current_index;
            if (get_ss_metadata_N(&source_symbols[current_index]->source_symbol)) {
                // let's get the packet number
                *packet_number = decode_u64(source_symbols[current_index]->source_symbol.chunk_data);
            }
            break;
        }
    }
    if (start_index == -1)
        return false;
    // now, find the end of the packet
    int end_index = -1;
    for (int current_index = recovered_symbol_idx ; current_index < n_source_symbols; current_index++) {
        if (!source_symbols[current_index]) {
            // the packet cannot be complete, so we failed reassembling it
            return false;
        }
        if (get_ss_metadata_E(&source_symbols[current_index]->source_symbol)) {
            end_index = current_index;
            break;
        }
    }
    if (end_index == -1)
        return false;

    *payload_size = 0;
    // we found the start and the end, let's reassemble the packet
    for (int i = start_index ; i <= end_index ; i++) {
        // TODO: uncomment this when the malloc limit is removed
//        if (*payload_size + source_symbols[i]->source_symbol.chunk_size > buffer_size) {
//            PROTOOP_PRINTF(cnx, "PACKET IS TOO BIG TO BE REASSEMBLED !! %u > %u\n", *payload_size + source_symbols[i]->source_symbol.chunk_size, buffer_size);
//            // packet too big, should never happen
//            return false;
//        }
        // we skip the packet number if it is present in the symbol
        size_t data_offset = get_ss_metadata_N(&source_symbols[i]->source_symbol) ? sizeof(uint64_t) : 0;
        // FIXME: this might be a bad idea to interfere with the packet payload and remove the padding, but it is needed due to the maximum mallocable size...
        for (int j = data_offset ; j < source_symbols[i]->source_symbol.chunk_size ; j++) {
            // skip padding
            if (source_symbols[i]->source_symbol.chunk_data[j] != 0)
                break;
            data_offset++;
        }
        // copy the chunk without padding into the packet
        my_memcpy(&buffer[*payload_size], source_symbols[i]->source_symbol.chunk_data + data_offset, source_symbols[i]->source_symbol.chunk_size - data_offset);
        *payload_size += source_symbols[i]->source_symbol.chunk_size - data_offset;
    }

    PROTOOP_PRINTF(cnx, "SUCCESSFULLY REASSEMBLED !!\n");

    // the packet has been reassembled !

    *first_id_in_packet = source_symbols[start_index]->id;

    return true;
}


static __attribute__((always_inline)) window_recovered_frame_t *get_recovered_frame(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, size_t max_bytes) {
    size_t minimum_size = 2*sizeof(uint64_t);
    if (max_bytes < minimum_size) // not enough space
        return NULL;
    window_recovered_frame_t *rf = create_window_recovered_frame(cnx);
    if (!rf)
        return NULL;
    max_bytes -= minimum_size;
    while(!pq_is_empty(wff->recovered_packets) && max_bytes > sizeof(uint64_t) + sizeof(window_source_symbol_id_t)) {
        uint64_t pn = 0;
        uint64_t id = 0;
        pq_get_min_key_val(wff->recovered_packets, &pn, (void **) &id);
        add_packet_to_recovered_frame(cnx, rf, pn, id);
        max_bytes -= sizeof(uint64_t) + sizeof(window_source_symbol_id_t);
        pq_pop_min(wff->recovered_packets);
    }
    return rf;
}

static __attribute__((always_inline)) int try_to_recover(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, uint16_t symbol_size) {
    PROTOOP_PRINTF(cnx, "MAYBE TRY TO RECOVER SYMBOLS\n");
    remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, get_highest_contiguous_received_source_symbol(wff->symbols_tracker) + 1);
    uint16_t selected_source_symbols = 0, selected_repair_symbols = 0, n_missing_source_symbols = 0;
    window_source_symbol_id_t first_selected_id = 0;
    my_memset(wff->ss_recovery_buffer, 0, MAX_RECEIVE_BUFFER_SIZE*sizeof(window_source_symbol_t *));
    my_memset(wff->rs_recovery_buffer, 0, MAX_RECEIVE_BUFFER_SIZE*sizeof(window_repair_symbol_t *));
    select_source_and_repair_symbols(cnx, wff, wff->ss_recovery_buffer, &selected_source_symbols, &first_selected_id, wff->rs_recovery_buffer,
            &selected_repair_symbols, &n_missing_source_symbols, wff->missing_symbols_buffer);
    if (selected_source_symbols == 0 || selected_repair_symbols == 0 ||
        n_missing_source_symbols == 0) {
        PROTOOP_PRINTF(cnx, "NOTHING TO RECOVER\n");
        return 0;
    }
    // we here assume that the first protected symbol is encoded in the fec block number
    if (first_selected_id > wff->highest_removed && selected_repair_symbols >= n_missing_source_symbols) {
        recover_lost_symbols(cnx, wff, wff->ss_recovery_buffer, selected_source_symbols, first_selected_id, wff->rs_recovery_buffer, selected_repair_symbols,
                n_missing_source_symbols, wff->missing_symbols_buffer, symbol_size);
        // we don't free anything, it will be freed when new symbols are received
    }

    int err = 0;
    uint8_t *recovered_packet = my_malloc(cnx, PICOQUIC_MAX_PACKET_SIZE);
    if (!recovered_packet) {
        PROTOOP_PRINTF(cnx, "COULD NOT ALLOCATE THE RECOVERED PACKET\n");
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(recovered_packet, 0, PICOQUIC_MAX_PACKET_SIZE);
    PROTOOP_PRINTF(cnx, "BEFORE LOOP, %d MISSING SYMBOLS, first_id = %u\n", n_missing_source_symbols, first_selected_id);
    for (int i = 0 ; i < n_missing_source_symbols ; i++) {
        window_source_symbol_t *ss = wff->ss_recovery_buffer[wff->missing_symbols_buffer[i] - first_selected_id];
        if (ss) {
            ss->id = wff->missing_symbols_buffer[i];
            window_receive_source_symbol(cnx, wff, ss);
            size_t packet_size = 0;
            uint64_t packet_number = 0;
            window_source_symbol_id_t first_id_in_packet = 0;
            PROTOOP_PRINTF(cnx, "BEFORE REASSEMBLE\n");
            if (reassemble_packet_from_recovered_symbol(cnx, wff, recovered_packet, PICOQUIC_MAX_PACKET_SIZE, wff->ss_recovery_buffer, selected_source_symbols,
                                                        wff->missing_symbols_buffer[i] - first_selected_id, &packet_size, &packet_number, &first_id_in_packet)) {
                window_source_symbol_t *ss = wff->ss_recovery_buffer[wff->missing_symbols_buffer[i] - first_selected_id];
                PROTOOP_PRINTF(cnx, "REASSEMBLED SIZE = %lu, CRC OF SYMBOL = 0x%x\n", packet_size, crc32(0, ss->source_symbol._whole_data, symbol_size));
                // TODO: maybe process the packets at another moment ??
                // FIXME: we assume here a single-path context

                err = picoquic_decode_frames_without_current_time(cnx, recovered_packet, packet_size, 3, (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0));
                if (err) {
                    PROTOOP_PRINTF(cnx, "ERROR WHILE DECODING RECOVERED PACKET: %d\n", err);
                } else {
                    PROTOOP_PRINTF(cnx, "RECOVERED PACKET %lx SUCCESSFULLY PARSED, QUEUE SIZE BEFORE = %d\n", packet_number, wff->recovered_packets->size);
                    // record this packet recovery
                    pq_insert(wff->recovered_packets, packet_number, (void *) (uint64_t) first_id_in_packet);
                    PROTOOP_PRINTF(cnx, "QUEUE SIZE AFTER = %d\n", packet_number, wff->recovered_packets->size);
                }
            }
        }
    }
    my_free(cnx, recovered_packet);
    if (!pq_is_empty(wff->recovered_packets)) {
        window_recovered_frame_t *rf = get_recovered_frame(cnx, wff, 200);
        if (rf) {
            reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
            if (!slot) {
                my_free(cnx, rf);
                return PICOQUIC_ERROR_MEMORY;
            }
            my_memset(slot, 0, sizeof(reserve_frame_slot_t));
            slot->frame_type = FRAME_RECOVERED;
            slot->nb_bytes = 200;
            slot->frame_ctx = rf;
            slot->is_congestion_controlled = true;
            if (reserve_frames(cnx, 1, slot) != slot->nb_bytes) {
                return PICOQUIC_ERROR_MEMORY;
            }
            PROTOOP_PRINTF(cnx, "RESERVED RECOVERED FRAME\n");
        } else {
            PROTOOP_PRINTF(cnx, "COULD NOT GET A RF\n");
        }
    }
    return err;
}
