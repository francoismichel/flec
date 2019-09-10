
#ifndef PICOQUIC_FEC_H
#define PICOQUIC_FEC_H

#include <stdint.h>
#include <stdbool.h>
#include <picoquic.h>
#include "../helpers.h"
#include "fec_constants.h"
#include "utils.h"

#define SIMPLE_FEC_OPAQUE_ID 42

typedef struct {
    bool has_written_fpi_frame;
    bool has_written_repair_frame;
    bool is_incoming_packet_fec_protected;
    bool current_packet_is_lost;
    source_symbol_id_t current_id;
    uint64_t last_protected_slot;
    uint64_t last_fec_slot;
    uint8_t *current_packet;
    source_symbol_id_t current_packet_first_id;
    uint16_t current_packet_length;
    uint64_t current_packet_number;
    framework_sender_t framework_sender;
    framework_receiver_t framework_receiver;
    lost_packet_queue_t lost_packets;

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
        ss->_whole_data[0] &= 0x101U;
}

static __attribute__((always_inline)) void set_ss_metadata_E(source_symbol_t *ss, bool val) {
    if (val)
        ss->_whole_data[0] |= 0b001U;
    else
        ss->_whole_data[0] &= 0x110U;
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

static __attribute__((always_inline)) int preprocess_packet_payload(picoquic_cnx_t *cnx, const uint8_t *packet_payload, size_t payload_length, uint8_t *output_payload, size_t *total_size) {
    size_t offset_in_packet_payload = 0;
    size_t offset_in_output = 0;
    size_t consumed = 0;
    int pure_ack = 0;
    uint8_t type_byte;
    while(offset_in_packet_payload < payload_length) {
        type_byte = packet_payload[offset_in_packet_payload];
        bool to_ignore = type_byte == picoquic_frame_type_ack || type_byte == picoquic_frame_type_padding || type_byte == picoquic_frame_type_crypto_hs || type_byte == FRAME_FEC_SRC_FPI;
        int err = helper_skip_frame(cnx, output_payload + offset_in_packet_payload, payload_length - offset_in_packet_payload, &consumed, &pure_ack);
        if (err)
            return err;
        if (!to_ignore) {
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
    int err = preprocess_packet_payload(cnx, payload, payload_length, processed_payload + processed_length, &temp_length);
    if (err)
        return NULL;
    processed_length += temp_length;
    uint16_t padded_length = (processed_length % chunk_size == 0) ? processed_length : (chunk_size * (processed_length/chunk_size + 1));
    uint16_t padding_length = padded_length - processed_length;
    *n_chunks = padded_length / chunk_size;
    source_symbol_t **retval = (source_symbol_t **) my_malloc(cnx, MAX(*n_chunks, 1)*sizeof(source_symbol_t *));
    if (!retval)
        return NULL;
    my_memset(retval, 0, MAX(*n_chunks, 1)*sizeof(source_symbol_t *));
    if (*n_chunks == 0)
        return retval;
    size_t offset_in_payload = 0;
    for (int current_symbol = 0 ; current_symbol < *n_chunks ; current_symbol++) {
        source_symbol_t *symbol = create_larger_source_symbol(cnx, chunk_size, source_symbol_memory_size);    // chunk size == symbol size - 1
        if (!symbol)
            return NULL;
        switch (current_symbol) {
            case 0:
                // first symbol, copy including the padding
                my_memcpy(symbol->chunk_data + padding_length, processed_payload + offset_in_payload, chunk_size - padding_length);
                set_ss_metadata_N(symbol, true);    // this symbol contains the packet number
                set_ss_metadata_S(symbol, true);    // this is the first symbol of the packet
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


#endif //PICOQUIC_FEC_H
