
#ifndef PICOQUIC_FEC_H
#define PICOQUIC_FEC_H

#include <stdint.h>
#include <stdbool.h>
#include <picoquic.h>
#include "../helpers.h"
#include "fec_constants.h"
#include "utils.h"

#define SIMPLE_FEC_OPAQUE_ID 10

typedef struct {
    bool has_written_fpi_frame;
    bool has_written_repair_frame;
    bool has_written_fb_fec_repair_frame;
    bool has_written_recovered_frame;
    bool is_incoming_packet_fec_protected;
    bool current_packet_is_lost;

    // FIXME: remove this horrible booleans, necessary to handle corretly the skip_frame operation...
    bool is_in_skip_frame;

    // TODO: see if we can get rid of this counter and boolean
    int n_repair_frames_sent_since_last_feedback;
    bool handshake_finished;

    source_symbol_id_t current_repair_frame_first_protected_id;
    uint16_t current_repair_frame_n_protected_symbols;
    uint16_t current_repair_frame_n_repair_symbols;

    source_symbol_id_t current_id;

    uint64_t current_slot;

    // TODO: remove these slots
//    uint64_t last_protected_slot;
//    uint64_t last_fec_slot;

    uint64_t n_reserved_id_or_repair_frames;

    uint8_t *current_packet;
    source_symbol_id_t current_packet_first_id;
    uint16_t current_packet_length;
    uint64_t current_packet_number;
    framework_sender_t framework_sender;
    framework_receiver_t framework_receiver;
    lost_packet_queue_t lost_packets;
    recovered_packets_buffer_t recovered_packets;

    uint16_t symbol_size;
} plugin_state_t;

static __attribute__((always_inline)) plugin_state_t *initialize_plugin_state(picoquic_cnx_t *cnx)
{
    plugin_state_t *state = (plugin_state_t *) my_malloc(cnx, sizeof(plugin_state_t));
    if (!state) return NULL;
    my_memset(state, 0, sizeof(plugin_state_t));
    protoop_arg_t frameworks[2];
    protoop_arg_t schemes[2];
    // create_fec_schemes creates the receiver (0) and sender (1) FEC Schemes. If an error happens, ret != 0 and both schemes are freed by the protoop
    int ret = (int) run_noparam(cnx, "create_fec_schemes", 0, NULL, schemes);
    if (ret) {
        my_free(cnx, state);
        return NULL;
    }
    fec_scheme_t scheme_receiver = (fec_scheme_t) schemes[0];
    fec_scheme_t scheme_sender = (fec_scheme_t) schemes[1];
    protoop_arg_t args[2];
    args[0] = scheme_receiver;
    args[1] = scheme_sender;
    // create_fec_framework creates the receiver (0) and sender (1) FEC Frameworks. If an error happens, ret != 0 and both frameworks are freed by the protoop
    ret = (int) run_noparam(cnx, "create_fec_framework", 2, args, frameworks);
    if (ret) {
        my_free(cnx, state);
        return NULL;
    }
    state->framework_receiver = (framework_receiver_t) frameworks[0];
    state->framework_sender = (framework_sender_t) frameworks[1];
    state->handshake_finished = false;

    state->symbol_size = SYMBOL_SIZE;
    return state;
}

static __attribute__((always_inline)) plugin_state_t *get_plugin_state(picoquic_cnx_t *cnx)
{
    int allocated = 0;
    plugin_state_t **state_ptr = (plugin_state_t **) get_opaque_data(cnx, SIMPLE_FEC_OPAQUE_ID, sizeof(plugin_state_t *), &allocated);
    if (!state_ptr) return NULL;
    if (allocated) {
        *state_ptr = initialize_plugin_state(cnx);
    }
    return *state_ptr;
}




typedef struct source_symbol {
    uint16_t chunk_size;
    uint8_t *chunk_data;
    uint8_t *_whole_data;    // md + chunk data
} source_symbol_t;

static __attribute__((always_inline)) void set_ss_metadata_N(source_symbol_t *ss, bool val) {
    if (val)
        ss->_whole_data[0] |= 0b100U;
    else
        ss->_whole_data[0] &= 0b011U;
}

static __attribute__((always_inline)) void set_ss_metadata_S(source_symbol_t *ss, bool val) {
    if (val)
        ss->_whole_data[0] |= 0b010U;
    else
        ss->_whole_data[0] &= 0b101U;
}

static __attribute__((always_inline)) void set_ss_metadata_E(source_symbol_t *ss, bool val) {
    if (val)
        ss->_whole_data[0] |= 0b001U;
    else
        ss->_whole_data[0] &= 0b110U;
}

static __attribute__((always_inline)) bool get_ss_metadata_N(source_symbol_t *ss) {
    return ss->_whole_data[0] & 0b100U;
}

static __attribute__((always_inline)) bool get_ss_metadata_S(source_symbol_t *ss) {
    return ss->_whole_data[0] & 0b010U;
}

static __attribute__((always_inline)) bool get_ss_metadata_E(source_symbol_t *ss) {
    return ss->_whole_data[0] & 0b001U;
}

// creates a source symbol with a larger memory size for the structure to allow source symbols composition
static __attribute__((always_inline)) source_symbol_t *create_larger_source_symbol(picoquic_cnx_t *cnx, uint16_t chunk_size, size_t mem_size) {
    if (mem_size < sizeof(source_symbol_t))
        return NULL;
    source_symbol_t *ret = my_malloc(cnx, mem_size);
    if (!ret)
        return NULL;
    my_memset(ret, 0, mem_size);
    ret->_whole_data = my_malloc(cnx, chunk_size + 1);
    if (!ret->_whole_data){
        my_free(cnx, ret);
        return NULL;
    }
    ret->chunk_data = ret->_whole_data + 1;
    my_memset(ret->_whole_data, 0, chunk_size + 1);
    return ret;
}

static __attribute__((always_inline)) source_symbol_t *create_source_symbol(picoquic_cnx_t *cnx, uint16_t chunk_size) {
    return create_larger_source_symbol(cnx, chunk_size, sizeof(source_symbol_t));
}

static __attribute__((always_inline)) void delete_source_symbol(picoquic_cnx_t *cnx, source_symbol_t *ss) {
    my_free(cnx, ss->_whole_data);
    my_free(cnx, ss);
}

typedef struct repair_symbol {
    bool is_fb_fec;
    uint16_t payload_length;
    uint8_t *repair_payload;
} repair_symbol_t;


static __attribute__((always_inline)) repair_symbol_t *create_repair_symbol(picoquic_cnx_t *cnx, uint16_t symbol_size) {
    repair_symbol_t *ret = my_malloc(cnx, sizeof(repair_symbol_t));
    if (!ret)
        return NULL;
    my_memset(ret, 0, sizeof(repair_symbol_t));
    ret->repair_payload = my_malloc(cnx, symbol_size);
    if (!ret->repair_payload){
        my_free(cnx, ret);
        return NULL;
    }
    my_memset(ret->repair_payload, 0, symbol_size);
    return ret;
}

static __attribute__((always_inline)) void delete_repair_symbol(picoquic_cnx_t *cnx, repair_symbol_t *rs) {
    my_free(cnx, rs->repair_payload);
    my_free(cnx, rs);
}

static __attribute__((always_inline)) int preprocess_packet_payload(picoquic_cnx_t *cnx, uint8_t *packet_payload, size_t payload_length, uint8_t *output_payload, size_t *total_size) {
    size_t offset_in_packet_payload = 0;
    size_t offset_in_output = 0;
    size_t consumed = 0;
    int pure_ack = 0;
    uint8_t type_byte;
    while(offset_in_packet_payload < payload_length) {
        my_memcpy(&type_byte, &packet_payload[offset_in_packet_payload], sizeof(uint8_t));
        // TODO: voir pk consumed fait 1400 bytes, printer e type byte
//        type_byte = packet_payload[offset_in_packet_payload];
//        bool to_ignore = type_byte == picoquic_frame_type_ack || type_byte == picoquic_frame_type_padding || type_byte == picoquic_frame_type_crypto_hs || type_byte == FRAME_FEC_SRC_FPI;
        bool to_ignore = !PICOQUIC_IN_RANGE(type_byte, picoquic_frame_type_stream_range_min, picoquic_frame_type_stream_range_max);
        int err = helper_skip_frame(cnx, packet_payload + offset_in_packet_payload, payload_length - offset_in_packet_payload, &consumed, &pure_ack);
        if (err)
            return err;
        if (!to_ignore) {
            PROTOOP_PRINTF(cnx, "NOT IGNORE, CONSUMED = %lu\n", consumed);
            my_memcpy(output_payload + offset_in_output, packet_payload + offset_in_packet_payload, consumed);
            offset_in_output += consumed;
        }
        offset_in_packet_payload += consumed;
    }
    *total_size = offset_in_output;
    return 0;
}


// TODO: maybe move this in utils.h
static __attribute__((always_inline)) source_symbol_t **packet_payload_to_source_symbols(picoquic_cnx_t *cnx, uint8_t *payload,
        uint16_t payload_length, uint16_t symbol_size, uint64_t packet_number, uint16_t *n_chunks, size_t source_symbol_memory_size) {
    if (payload_length == 0)
        return NULL;
    uint8_t *processed_payload = my_malloc(cnx, payload_length + sizeof(uint64_t));
    if (!processed_payload) {
        return NULL;
    }
    uint16_t chunk_size = symbol_size - 1;
    // add the packet number at the beginning of the payload we do it anyway, even if the design allows us to not encode it
    encode_u64(packet_number, processed_payload);
    size_t processed_length = sizeof(packet_number);
    size_t temp_length = 0;
    // remove the useless frames from the payload
    PROTOOP_PRINTF(cnx, "BEFORE PREPROCESS\n");
    int err = preprocess_packet_payload(cnx, payload, payload_length, processed_payload + processed_length, &temp_length);
    if (err)
        return NULL;
    PROTOOP_PRINTF(cnx, "AFTER PREPROCESS, PAYLOAD LENGTH = %u, TEMP LENGTH = %u\n", payload_length, temp_length);
    processed_length += temp_length;
    uint16_t padded_length = (processed_length % chunk_size == 0) ? processed_length : (chunk_size * (processed_length/chunk_size + 1));
    uint16_t padding_length = padded_length - processed_length;
    *n_chunks = padded_length / chunk_size;
    PROTOOP_PRINTF(cnx, "PROCESSED LENGTH = %u, PADDED LENGTH = %u, CHUNK SIZE = %u, N_CHUNKS = %u\n", processed_length, padded_length, chunk_size, *n_chunks);
    source_symbol_t **retval = (source_symbol_t **) my_malloc(cnx, MAX(*n_chunks, 1)*sizeof(source_symbol_t *));
    if (!retval)
        return NULL;
    my_memset(retval, 0, MAX(*n_chunks, 1)*sizeof(source_symbol_t *));
    if (*n_chunks == 0)
        return retval;
    size_t offset_in_payload = 0;
    // TODO: print the first bytes of the symbol to see if we badly encode the metadata byte
    for (int current_symbol = 0 ; current_symbol < *n_chunks ; current_symbol++) {
        source_symbol_t *symbol = create_larger_source_symbol(cnx, chunk_size, source_symbol_memory_size);    // chunk size == symbol size - 1
        if (!symbol)
            return NULL;
        switch (current_symbol) {
            case 0:
                set_ss_metadata_N(symbol, true);    // this symbol contains the packet number
                set_ss_metadata_S(symbol, true);    // this is the first symbol of the packet
                // copy the packet payload
                my_memcpy(symbol->chunk_data, processed_payload, sizeof(uint64_t));
                offset_in_payload += sizeof(uint64_t);
                // first symbol, copy including the padding
                my_memcpy(symbol->chunk_data + sizeof(uint64_t) + padding_length, processed_payload + offset_in_payload, chunk_size - padding_length - sizeof(uint64_t));
                offset_in_payload += chunk_size - padding_length;
                break;
            default:
                my_memcpy(symbol->chunk_data, processed_payload + offset_in_payload, chunk_size);
                set_ss_metadata_N(symbol, false);
                offset_in_payload += chunk_size;
                break;
        }

        retval[current_symbol] = symbol;
    }
    set_ss_metadata_E(retval[*n_chunks-1], true);   // this is the last symbol of the packet
    return retval;
}



static __attribute__((always_inline)) int maybe_notify_recovered_packets_to_everybody(picoquic_cnx_t *cnx,
                                                                                       recovered_packets_buffer_t *b,
                                                                                       uint64_t current_time) {
    // TODO: handle multipath
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
            uint64_t pn64 = dequeue_recovered_packet_from_buffer(b);
            plugin_state_t *state = get_plugin_state(cnx);
            if (!state)
                return PICOQUIC_ERROR_MEMORY;
            uint64_t slot;
            source_symbol_id_t first_id;
            uint16_t n_source_symbols;


            // announce the reception of the source symbols
            bool present = dequeue_lost_packet(cnx, &state->lost_packets, pn64, &slot, &first_id, &n_source_symbols);
            if (present) {
                fec_packet_symbols_have_been_received(cnx, pn64, slot, first_id, n_source_symbols, true, false);
            } else {
                // this is not normal
                PROTOOP_PRINTF(cnx, "ERROR: THE RECOVERED PACKET %lx (%lu) IS NEITHER IN THE RETRANSMIT QUEUE, NEITHER IN THE LOST PACKETS\n", pn64, pn64);
                return -1;
            }
        } // else, do nothing, try the next packet
        current_packet = pnext;
    }
    return 0;
}


#endif //PICOQUIC_FEC_H
