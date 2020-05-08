#include <zlib.h>
#include "../../helpers.h"
#include "window_receive_buffers.h"
#include "types.h"
#include "fec_schemes/online_rlc_gf256/headers/equation.h"
#include "fec_schemes/online_rlc_gf256/headers/arraylist.h"

//#define MAX_RECEIVE_BUFFER_SIZE 1001


#define MAX_AMBIGUOUS_ID_GAP ((uint32_t) 0x200*2)       // the max ambiguous ID gap depends on the max number of source symbols that can be protected by a Repair Symbol


typedef struct {
    // those two booleans are used to avoid a call-cycle in the protoops
    // when we receive a symbol, we mark the event and we try to recover new packets
    // afterwards
    bool has_received_a_source_symbol;
    bool has_received_a_repair_symbol;
    bool a_window_frame_has_been_written;
    bool a_window_frame_has_been_lost;

    uint32_t highest_removed;
    uint32_t receive_buffer_size;

    window_source_symbol_id_t last_acknowledged_smallest_considered_id;
    window_source_symbol_id_t smallest_considered_id_for_which_rwin_frame_has_been_sent;
    window_source_symbol_id_t smallest_considered_id_to_advertise;

    fec_scheme_t fs;
    received_symbols_tracker_t symbols_tracker;
    ring_based_received_source_symbols_buffer_t *received_source_symbols;

    min_max_pq_t recovered_packets;

    arraylist_t recovered_symbols;

    uint8_t packet_sized_buffer[PICOQUIC_MAX_PACKET_SIZE];

} window_fec_framework_receiver_t;

static __attribute__((always_inline)) window_fec_framework_receiver_t *create_framework_receiver(picoquic_cnx_t *cnx, fec_scheme_t fs, uint16_t symbol_size, uint64_t bytes_repair_buffer, size_t receive_buffer_size) {

    window_fec_framework_receiver_t *wff = my_malloc(cnx, sizeof(window_fec_framework_receiver_t));
    if (!wff)
        return NULL;
    my_memset(wff, 0, sizeof(window_fec_framework_receiver_t));
    wff->fs = fs;
    wff->received_source_symbols = new_ring_based_source_symbols_buffer(cnx, receive_buffer_size);
    if (!wff->received_source_symbols) {
        my_free(cnx, wff);
        return NULL;
    }
    wff->symbols_tracker = create_received_symbols_tracker(cnx, WINDOW_INITIAL_SYMBOL_ID);
    if (!wff->symbols_tracker) {
        release_ring_based_source_symbols_buffer(cnx, wff->received_source_symbols);
        my_free(cnx, wff);
        return NULL;
    }
    wff->recovered_packets = create_min_max_pq(cnx, receive_buffer_size);
    if (!wff->symbols_tracker) {
        release_ring_based_source_symbols_buffer(cnx, wff->received_source_symbols);
        delete_received_symbols_tracker(cnx, wff->symbols_tracker);
        my_free(cnx, wff);
        return NULL;
    }
    wff->receive_buffer_size = receive_buffer_size;
    wff->smallest_considered_id_to_advertise = 1;
    arraylist_init(cnx, &wff->recovered_symbols, 10);
    return wff;
}

static __attribute__((always_inline)) int recover_lost_symbols(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, uint16_t symbol_size, arraylist_t *recorered_symbols, protoop_arg_t *recovered) {
    protoop_arg_t args[3];
    args[0] = (protoop_arg_t) wff->fs;
    args[1] = (protoop_arg_t) symbol_size;
    args[2] = (protoop_arg_t) recorered_symbols;

    return (int) run_noparam(cnx, FEC_PROTOOP_WINDOW_FEC_SCHEME_RECOVER, 3, args, recovered);
}

static __attribute__((always_inline)) int window_update_flow_control_infos(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff){
    window_source_symbol_id_t highest_contiguous = get_highest_contiguous_received_source_symbol(wff->symbols_tracker);
//    if (wff->smallest_considered_id_to_advertise + wff->receive_buffer_size/2 <= highest_contiguous) {
        wff->smallest_considered_id_to_advertise = highest_contiguous;
//    }
    return 0;
}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int window_receive_source_symbol(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff, window_source_symbol_t *ss){
    window_source_symbol_id_t removed_id = 0;
    ring_based_source_symbols_buffer_add_source_symbol(cnx, wff->received_source_symbols, ss);
    wff->highest_removed = MAX(wff->highest_removed, ring_based_source_symbols_buffer_get_first_source_symbol_id(cnx, wff->received_source_symbols));
    PROTOOP_PRINTF(cnx, "ADD SOURCE SYMBOL %u, NEW HIGHEST REMOVED = \n", ss->id);
//    if (removed) {
//        wff->highest_removed = MAX(removed_id, wff->highest_removed);
//        PROTOOP_PRINTF(cnx, "REMOVED SOURCE SYMBOL %u (whole data = %p) TO ADD SYMBOL %u (whole data = %p)\n", removed->id, (protoop_arg_t) removed->source_symbol._whole_data, ss->id, (protoop_arg_t) ss->source_symbol._whole_data);
//        delete_window_source_symbol(cnx, removed);
//    }
    uint32_t highest_contiguous_received = get_highest_contiguous_received_source_symbol(wff->symbols_tracker);
    PROTOOP_PRINTF(cnx, "HIGHEST_CONTIGUOUSLY_RECEIVED = %u\n", highest_contiguous_received);
    int err = tracker_receive_source_symbol(cnx, wff->symbols_tracker, ss->id);
    if (err) {
        return err;
    }
    equation_t *removed_equation = NULL;
    int used_in_system = 0;
    window_fec_scheme_receive_source_symbol(cnx, wff->fs, ss, (void **) &removed_equation, &used_in_system);
    if (removed_equation != NULL) {
//        if (!removed_equation->is_from_source) {
            // it was a repair symbol, we can discard it completely
            equation_free(cnx, removed_equation);
//        } else {
//            // it comes from a source symbol, it is still useful when receiving new repair symbols, so keep the payload !
//            equation_free_keep_repair_payload(cnx, removed_equation);
//        }
    }
    wff->has_received_a_source_symbol = true;
    if (highest_contiguous_received != get_highest_contiguous_received_source_symbol(wff->symbols_tracker)) {
        // let's find all the blocks protecting this symbol to see if we can recover the remaining
        // we don't recover symbols if we already are in recovery mode
        fec_scheme_remove_unused_repair_symbols(cnx, wff->fs, get_highest_contiguous_received_source_symbol(wff->symbols_tracker));
//        remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, get_highest_contiguous_received_source_symbol(wff->symbols_tracker) + 1);
    }
    window_update_flow_control_infos(cnx, wff);
    return 0;
}


static __attribute__((always_inline)) int window_receive_packet_payload(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff,
        uint8_t *payload, size_t payload_length, uint64_t packet_number, window_source_symbol_id_t first_symbol_id, size_t symbol_size) {
    uint16_t n_chunks = 0;
    source_symbol_t **sss = packet_payload_to_source_symbols(cnx, payload, payload_length, symbol_size, packet_number, &n_chunks, sizeof(window_source_symbol_t), wff->packet_sized_buffer);
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
    if (rs->metadata.first_id + rs->metadata.n_protected_symbols - 1 <= get_highest_contiguous_received_source_symbol(wff->symbols_tracker)) {
        PROTOOP_PRINTF(cnx, "DON'T ADD REPAIR SYMBOL: CONCERN NO INTERESTING SOURCE SYMBOL\n");
        return false;
    }

    equation_t *removed_equation = NULL;
    int used_in_system = 0;
    PROTOOP_PRINTF(cnx, "BEFORE FEC SCHEME RECEIVE REPAIR SYMBOL, rs[0, 1, 2, 3] = %u, %u, %u, %u\n", rs->repair_symbol.repair_payload[0], rs->repair_symbol.repair_payload[1], rs->repair_symbol.repair_payload[2], rs->repair_symbol.repair_payload[3]);

    fec_scheme_receive_repair_symbol(cnx, wff->fs, rs, (void **) &removed_equation, &used_in_system);
    PROTOOP_PRINTF(cnx, "AFTER FEC SCHEME RECEIVE REPAIR SYMBOL\n");

    if (removed_equation != NULL) {
//        if (!removed_equation->is_from_source) {
            // it was a repair symbol, we can discard it completely
            equation_free(cnx, removed_equation);
//        } else {
//            // it comes from a source symbol, it is still useful when receiving new repair symbols, so keep the payload !
//            equation_free_keep_repair_payload(cnx, removed_equation);
//        }
    }

    if (!used_in_system) {
        delete_window_repair_symbol(cnx, rs);
    }

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
        size_t buffer_size, ring_based_received_source_symbols_buffer_t *source_symbols, window_source_symbol_t *recovered_symbol,
        size_t *payload_size, uint64_t *packet_number, window_source_symbol_id_t *first_id_in_packet) {
    // first, find the start of the packet backwards
    window_source_symbol_id_t recovered_symbol_id = recovered_symbol->id;
    window_source_symbol_id_t start_id = 0;
    window_source_symbol_id_t end_id = 0;
    window_source_symbol_id_t first_id_in_buffer = ring_based_source_symbols_buffer_get_first_source_symbol_id(cnx, source_symbols);
    PROTOOP_PRINTF(cnx, "TRY TO REASSEMBLE\n");
    if (get_ss_metadata_S(&recovered_symbol->source_symbol) && get_ss_metadata_E(&recovered_symbol->source_symbol)) {
        PROTOOP_PRINTF(cnx, "ALL IN ONE FOR %u\n", recovered_symbol->id);
        start_id = end_id = recovered_symbol->id;
        if (get_ss_metadata_N(&recovered_symbol->source_symbol)) {
            // let's get the packet number
            *packet_number = decode_u64(recovered_symbol->source_symbol.chunk_data);
        }
    } else {
        PROTOOP_PRINTF(cnx, "ELSE FOR %u\n", recovered_symbol->id);

        for (window_source_symbol_id_t current_id = recovered_symbol_id ; current_id >= first_id_in_buffer; current_id--) {
            PROTOOP_PRINTF(cnx, "CONSIDER SYMBOL AT INDEX %d\n", current_id);
            if ( !ring_based_source_symbols_buffer_contains(cnx, source_symbols, current_id)) {
                // the packet cannot be complete, so we failed reassembling it
                return false;
            }
            window_source_symbol_t *ss = ring_based_source_symbols_buffer_get(cnx, source_symbols, current_id);
            if (get_ss_metadata_S(&ss->source_symbol)) {
                start_id = current_id;
                if (get_ss_metadata_N(&ss->source_symbol)) {
                    // let's get the packet number
                    *packet_number = decode_u64(ss->source_symbol.chunk_data);
                }
                break;
            }
        }
        if (start_id == 0)
            return false;
        // now, find the end of the packet
        for (window_source_symbol_id_t current_id = recovered_symbol_id ; current_id <= ring_based_source_symbols_buffer_get_last_source_symbol_id(cnx, source_symbols); current_id++) {
            if (!ring_based_source_symbols_buffer_contains(cnx, source_symbols, current_id)) {
                // the packet cannot be complete, so we failed reassembling it
                return false;
            }
            if (get_ss_metadata_E(&ring_based_source_symbols_buffer_get(cnx, source_symbols, current_id)->source_symbol)) {
                end_id = current_id;
                break;
            }
        }
        if (end_id == -1)
            return false;
    }

    *payload_size = 0;
    // we found the start and the end, let's reassemble the packet
    for (window_source_symbol_id_t i = start_id ; i <= end_id ; i++) {
        // TODO: uncomment this when the malloc limit is removed
//        if (*payload_size + source_symbols[i]->source_symbol.chunk_size > buffer_size) {
//            PROTOOP_PRINTF(cnx, "PACKET IS TOO BIG TO BE REASSEMBLED !! %u > %u\n", *payload_size + source_symbols[i]->source_symbol.chunk_size, buffer_size);
//            // packet too big, should never happen
//            return false;
//        }
        // we skip the packet number if it is present in the symbol
        window_source_symbol_t *ss = NULL;
        if (i == recovered_symbol_id) {
            ss = recovered_symbol;
        } else {
            ss = ring_based_source_symbols_buffer_get(cnx, source_symbols, i);
        }
        size_t data_offset = get_ss_metadata_N(&ss->source_symbol) ? sizeof(uint64_t) : 0;
        // FIXME: this might be a bad idea to interfere with the packet payload and remove the padding, but it is needed due to the maximum mallocable size...
        for (int j = data_offset ; j < ss->source_symbol.chunk_size ; j++) {
            // skip padding
            if (ss->source_symbol.chunk_data[j] != 0)
                break;
            data_offset++;
        }
        // copy the chunk without padding into the packet
        PROTOOP_PRINTF(cnx, "BEFORE MEMCPY %p TO %p, %lu bytes, CHUNK SIZE = %u\n", (protoop_arg_t) &buffer[*payload_size], (protoop_arg_t) ss->source_symbol.chunk_data + data_offset, (protoop_arg_t) ss->source_symbol.chunk_size - data_offset, ss->source_symbol.chunk_size);
        my_memcpy(&buffer[*payload_size], ss->source_symbol.chunk_data + data_offset, ss->source_symbol.chunk_size - data_offset);
        PROTOOP_PRINTF(cnx, "AFTER MEMCPY\n");
        *payload_size += ss->source_symbol.chunk_size - data_offset;
    }

    PROTOOP_PRINTF(cnx, "SUCCESSFULLY REASSEMBLED !!\n");

    // the packet has been reassembled !

    *first_id_in_packet = start_id;

    return true;
}

static __attribute__((always_inline)) int _reserve_window_rwin_frame(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff) {
    window_rwin_frame_t *frame = create_window_rwin_frame(cnx);
    if (frame) {
        reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
        if (!slot) {
            my_free(cnx, frame);
            return PICOQUIC_ERROR_MEMORY;
        }
        my_memset(slot, 0, sizeof(reserve_frame_slot_t));
        // !!! this will likely be overwritten by the frame writer it it wants the most up to date flow control information
        frame->smallest_id = wff->smallest_considered_id_to_advertise;
        frame->window_size = wff->receive_buffer_size;
        slot->frame_type = FRAME_WINDOW_RWIN;
        slot->nb_bytes = 17;    // at worst: type byte + 8 + 8
        slot->frame_ctx = frame;
        slot->is_congestion_controlled = false;
        if (reserve_frames(cnx, 1, slot) != slot->nb_bytes) {
            return PICOQUIC_ERROR_MEMORY;
        }
        wff->smallest_considered_id_for_which_rwin_frame_has_been_sent = wff->smallest_considered_id_to_advertise;
        return 0;
    }
    return PICOQUIC_ERROR_MEMORY;
}

static __attribute__((always_inline)) bool needs_a_rwin_frame(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff) {

    picoquic_stream_head *stream = (picoquic_stream_head *) get_cnx(cnx, AK_CNX_FIRST_STREAM, 0);
    if (wff->last_acknowledged_smallest_considered_id == 0 ||
        //wff->last_acknowledged_smallest_considered_id + wff->receive_buffer_size/2 < get_highest_contiguous_received_source_symbol(wff->symbols_tracker);
        (wff->a_window_frame_has_been_lost || wff->smallest_considered_id_for_which_rwin_frame_has_been_sent + 1*wff->receive_buffer_size/3 < get_highest_contiguous_received_source_symbol(wff->symbols_tracker))) {
        return true;
    }
    bool maxdata_ready = false;
    uint64_t new_offset = 0;
    while(!maxdata_ready && stream) {
        maxdata_ready |= helper_is_max_stream_data_frame_required(cnx, stream, &new_offset);
        stream = (picoquic_stream_head *) get_stream_head(stream, AK_STREAMHEAD_NEXT_STREAM);
    }
    return maxdata_ready;
}

static __attribute__((always_inline)) int reserve_window_rwin_frame_if_needed(picoquic_cnx_t *cnx, window_fec_framework_receiver_t *wff) {
    if (needs_a_rwin_frame(cnx, wff)) {
        return _reserve_window_rwin_frame(cnx, wff);
    }
    return 0;
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
//    remove_and_free_unused_repair_symbols(cnx, wff->received_repair_symbols, get_highest_contiguous_received_source_symbol(wff->symbols_tracker) + 1);
    fec_scheme_remove_unused_repair_symbols(cnx, wff->fs, get_highest_contiguous_received_source_symbol(wff->symbols_tracker));
    protoop_arg_t could_recover = false;
//    if (first_selected_id > wff->highest_removed /*&& selected_repair_symbols >= n_missing_source_symbols*/) {
        recover_lost_symbols(cnx, wff, symbol_size, &wff->recovered_symbols, &could_recover);
        // we don't free anything, it will be freed when new symbols are received
//    }

    int err = 0;
    my_memset(wff->packet_sized_buffer, 0, PICOQUIC_MAX_PACKET_SIZE);
    for (int i = 0 ; could_recover && i < arraylist_size(&wff->recovered_symbols) ; i++) {
        window_source_symbol_t *ss = (window_source_symbol_t *) arraylist_get(&wff->recovered_symbols, i);
        if (ss) {
            window_receive_source_symbol(cnx, wff, ss);
            size_t packet_size = 0;
            uint64_t packet_number = 0;
            window_source_symbol_id_t first_id_in_packet = 0;
            PROTOOP_PRINTF(cnx, "BEFORE REASSEMBLE %u\n", ss->id);
            if (reassemble_packet_from_recovered_symbol(cnx, wff, wff->packet_sized_buffer, PICOQUIC_MAX_PACKET_SIZE, wff->received_source_symbols, ss, &packet_size, &packet_number, &first_id_in_packet)) {
                PROTOOP_PRINTF(cnx, "REASSEMBLED SIZE = %lu, CRC OF SYMBOL = 0x%x\n", packet_size, crc32(0, ss->source_symbol._whole_data, symbol_size));
                // TODO: maybe process the packets at another moment ??
                // FIXME: we assume here a single-path context

                err = picoquic_decode_frames_without_current_time(cnx, wff->packet_sized_buffer, packet_size, 3, (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0));
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
    arraylist_reset(&wff->recovered_symbols);

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

static __attribute__((always_inline)) int window_set_buffers_sizes(picoquic_cnx_t *cnx, size_t source_symbols_buffer_size, size_t repair_symbols_buffer_size) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    fec_scheme_set_max_rs(cnx, wff->fs, repair_symbols_buffer_size);

    release_ring_based_source_symbols_buffer(cnx, wff->received_source_symbols);
    wff->received_source_symbols = new_ring_based_source_symbols_buffer(cnx, source_symbols_buffer_size);

    wff->receive_buffer_size = source_symbols_buffer_size;

    wff->smallest_considered_id_to_advertise = 1;
    wff->smallest_considered_id_for_which_rwin_frame_has_been_sent = 0;
    wff->last_acknowledged_smallest_considered_id = 0;
    return 0;
}
