#include "../../helpers.h"
#include "../fec_protoops.h"

// returns the length of the symbol
protoop_arg_t packet_payload_to_source_symbol(picoquic_cnx_t *cnx)
{
    uint8_t *bytes_protected = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint8_t *buffer = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t payload_length = (uint32_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    uint64_t sequence_number = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 3);
    bpf_state *state = get_bpf_state(cnx);
    state->current_symbol_length = FEC_SOURCE_SYMBOL_OVERHEAD + (uint16_t) payload_length;
    if (!buffer) {
        return PICOQUIC_ERROR_MEMORY;
    }
    encode_u64(sequence_number, buffer + 1);
    buffer[0] = FEC_MAGIC_NUMBER;
    uint16_t offset_in_symbol = 0;
    uint16_t offset_in_packet_payload = 0;
    size_t consumed = 0;
    int pure_ack = 0;
    uint8_t first_byte = 0;
    // we copy the whole thing once to avoid doing many memcpy for the type byte
    my_memcpy(buffer + FEC_SOURCE_SYMBOL_OVERHEAD, bytes_protected, payload_length);
    uint8_t *to_copy = bytes_protected;
    while(offset_in_packet_payload < payload_length) {
        first_byte = buffer[FEC_SOURCE_SYMBOL_OVERHEAD + offset_in_packet_payload];                                                                         // forced to remove this to avoid problems when erasing the id
        bool to_ignore = first_byte == picoquic_frame_type_ack || first_byte == picoquic_frame_type_padding || first_byte == picoquic_frame_type_crypto_hs /*|| first_byte == SOURCE_FPID_TYPE*/;
        helper_skip_frame(cnx, to_copy + offset_in_packet_payload, payload_length - offset_in_packet_payload, &consumed, &pure_ack);
        if (!to_ignore) {
            if (first_byte == SOURCE_FPID_TYPE) state->sfpid_frame_position_in_current_packet_payload = offset_in_symbol;
            my_memcpy(buffer + FEC_SOURCE_SYMBOL_OVERHEAD + offset_in_symbol, to_copy + offset_in_packet_payload, consumed);
            offset_in_symbol += consumed;
        }
        offset_in_packet_payload += consumed;
    }
    PROTOOP_PRINTF(cnx, "SKIPPED %d BYTES IN SOURCE SYMBOL, SYMBOL SIZE = %d\n", offset_in_packet_payload - offset_in_symbol, 1 + sizeof(uint64_t) + offset_in_symbol);
    return FEC_SOURCE_SYMBOL_OVERHEAD + offset_in_symbol;
}