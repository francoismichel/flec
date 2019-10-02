
#ifndef PICOQUIC_COMPRESSED_REPAIR_FRAME_H
#define PICOQUIC_COMPRESSED_REPAIR_FRAME_H


#include "../types.h"

static __attribute__((always_inline)) size_t predict_compressed_window_repair_frame(picoquic_cnx_t *cnx, window_repair_frame_t *repair_frame, uint16_t symbol_size) {
    size_t retval = 0;
    // encode fec-scheme-specific
    retval += sizeof(repair_frame->fss.val);
    retval += sizeof(window_source_symbol_id_t);
    // encode number of repair symbols (the symbol size is implicitly negociated so we don't need to encode it)
    retval += sizeof(repair_frame->n_protected_symbols);
    retval += sizeof(repair_frame->n_repair_symbols);
    uint64_t buffer;
    PROTOOP_PRINTF(cnx, "BEFORE PREDICT LOOP, RF = %p\n", (protoop_arg_t) retval);
    // encode payload
    for (int i = 0 ; i < repair_frame->n_repair_symbols ; i++) {
        PROTOOP_PRINTF(cnx, "ITERATION %d\n", i);
        // FIXME: maybe should we remove the symbol size field from the repair symbols
        if (repair_frame->symbols[i]->payload_length != symbol_size) {
            PROTOOP_PRINTF(cnx, "ERROR: INCONSISTENT REPAIR SYMBOL SIZE= RS LENGTH = %u, SYMBOL SIZE = %u\n", repair_frame->symbols[i]->payload_length, symbol_size);
            return -1;
        }
        // encoding the ith symbol
        // first find the padding length
        int padding_index = 0;
        // we skip the potential chunk byte and packet number
        for (int j = 1 + sizeof(uint64_t) ; j < symbol_size ; j++) {
            if (repair_frame->symbols[i]->repair_payload[j] == 0) {
                PROTOOP_PRINTF(cnx, "FOUND PADDING AT INDEX %d\n", j);
                padding_index = j;
                break;
            }
        }
        uint16_t padding_length = 0;
        for (int j = padding_index ; j < symbol_size ; j++) {
            if (repair_frame->symbols[i]->repair_payload[j] == 0)
                padding_length++;
            else
                break;
        }
        retval += picoquic_varint_encode((uint8_t *) &buffer, sizeof(buffer), padding_index);
        retval += picoquic_varint_encode((uint8_t *) &buffer, sizeof(buffer), padding_length);

        // TODO: copy in two times, ignoring the padding
        if (padding_index) {
            retval += padding_index;
        }
        retval += symbol_size - padding_index - padding_length;

    }
    PROTOOP_PRINTF(cnx, "END\n");
    return retval;
}



static __attribute__((always_inline)) int serialize_compress_padding_window_repair_frame(picoquic_cnx_t *cnx, uint8_t *out_buffer, size_t buffer_length, window_repair_frame_t *repair_frame, uint16_t symbol_size, size_t *consumed) {
//    if (buffer_length < REPAIR_FRAME_HEADER_SIZE + repair_frame->n_repair_symbols*symbol_size)
//        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    *consumed = 0;
    // encode fec-scheme-specific
    my_memcpy(out_buffer, repair_frame->fss.val, sizeof(repair_frame->fss.val));
    *consumed += sizeof(repair_frame->fss.val);
    size_t tmp = 0;
    // encode symbol id
    int err = serialize_window_source_symbol_id(out_buffer + *consumed, buffer_length, repair_frame->first_protected_symbol, &tmp);
    if (err)
        return err;
    *consumed += tmp;
    // encode number of repair symbols (the symbol size is implicitly negociated so we don't need to encode it)
    encode_un(repair_frame->n_protected_symbols, out_buffer + *consumed, sizeof(repair_frame->n_protected_symbols));
    *consumed += sizeof(repair_frame->n_protected_symbols);
    encode_un(repair_frame->n_repair_symbols, out_buffer + *consumed, sizeof(repair_frame->n_repair_symbols));
    *consumed += sizeof(repair_frame->n_repair_symbols);
    // encode payload
    for (int i = 0 ; i < repair_frame->n_repair_symbols ; i++) {
        // FIXME: maybe should we remove the symbol size field from the repair symbols
        if (repair_frame->symbols[i]->payload_length != symbol_size) {
            PROTOOP_PRINTF(cnx, "ERROR: INCONSISTENT REPAIR SYMBOL SIZE= RS LENGTH = %u, SYMBOL SIZE = %u\n", repair_frame->symbols[i]->payload_length, symbol_size);
            return -1;
        }
        // encoding the ith symbol
        // first find the padding index
        int padding_index = 0;
        for (int j = 1 + sizeof(uint64_t) ; j < symbol_size ; j++) {
            if (repair_frame->symbols[i]->repair_payload[j] == 0) {
                padding_index = j;
                break;
            }
        }
        uint16_t padding_length = 0;
        for (int j = padding_index ; j < symbol_size ; j++) {
            if (repair_frame->symbols[i]->repair_payload[j] == 0)
                padding_length++;
            else
                break;
        }
        *consumed += picoquic_varint_encode(out_buffer + *consumed, symbol_size, padding_index);
        *consumed += picoquic_varint_encode(out_buffer + *consumed, symbol_size, padding_length);

        // TODO: copy in two times, ignoring the padding
        if (padding_index) {
            my_memcpy(out_buffer + *consumed, repair_frame->symbols[i]->repair_payload, padding_index);
            *consumed += padding_index;
        }
        my_memcpy(out_buffer + *consumed, repair_frame->symbols[i]->repair_payload + padding_index + padding_length, symbol_size - padding_index - padding_length);
        *consumed += symbol_size - padding_index - padding_length;

        PROTOOP_PRINTF(cnx, "FIRST BYTES = %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx\n", repair_frame->symbols[i]->repair_payload[0],
                       repair_frame->symbols[i]->repair_payload[1], repair_frame->symbols[i]->repair_payload[2],
                       repair_frame->symbols[i]->repair_payload[3], repair_frame->symbols[i]->repair_payload[4],
                       repair_frame->symbols[i]->repair_payload[5], repair_frame->symbols[i]->repair_payload[6], repair_frame->symbols[i]->repair_payload[7],
                       repair_frame->symbols[i]->repair_payload[8], repair_frame->symbols[i]->repair_payload[9]);

    }
    return 0;
}


static __attribute__((always_inline)) window_repair_frame_t *parse_compressed_padding_window_repair_frame(picoquic_cnx_t *cnx, uint8_t *bytes, const uint8_t *bytes_max,
                                                                                                          uint16_t symbol_size, size_t *consumed, bool skip_repair_payload) {
    PROTOOP_PRINTF(cnx, "PARSE COMPRESSED\n");
    *consumed = 0;
    window_repair_frame_t *rf = create_repair_frame_without_symbols(cnx);
    if (!rf)
        return NULL;
    // encode fec-scheme-specific

    my_memcpy(rf->fss.val, bytes, sizeof(rf->fss.val));
    *consumed += sizeof(rf->fss.val);
    size_t tmp = 0;
    // decode symbol id
    int err = decode_window_source_symbol_id(bytes + *consumed, bytes_max - bytes - *consumed, &rf->first_protected_symbol, &tmp);
    if (err)
        return NULL;
    *consumed += tmp;
    // encode number of repair symbols (the symbol size is implicitly negociated so we don't need to encode it)
    rf->n_protected_symbols = decode_un(bytes + *consumed, sizeof(rf->n_protected_symbols));
    *consumed += sizeof(rf->n_protected_symbols);
    rf->n_repair_symbols = decode_un(bytes + *consumed, sizeof(rf->n_repair_symbols));
    *consumed += sizeof(rf->n_repair_symbols);

    PROTOOP_PRINTF(cnx, "ENTER IN IF, N_RS = %d\n", rf->n_repair_symbols);
    if (!skip_repair_payload) {
        rf->symbols = my_malloc(cnx, rf->n_repair_symbols*sizeof(window_repair_symbol_t *));
        if (!rf->symbols) {
            delete_repair_frame(cnx, rf);
            *consumed = 0;
            return NULL;
        }
        my_memset(rf->symbols, 0, rf->n_repair_symbols*sizeof(window_repair_symbol_t *));
    }
    // decode payload
    for (int i = 0 ; i < rf->n_repair_symbols ; i++) {
        // decoding the ith symbol
        window_repair_symbol_t *rs = NULL;
        if (!skip_repair_payload) {
            rs = create_window_repair_symbol(cnx, symbol_size);
            rf->symbols[i] = (repair_symbol_t *) rs;
            if (!rf->symbols[i]) {
                for (int j = 0 ; j < i ; j++) {
                    delete_repair_symbol(cnx, rf->symbols[j]);
                }
                delete_repair_frame(cnx, rf);
                *consumed = 0;
                break;
            }
            rs->metadata.first_id = rf->first_protected_symbol;
            rs->metadata.n_protected_symbols = rf->n_protected_symbols;
            encode_u32(decode_u32(rf->fss.val) + i, rs->metadata.fss.val);
        }

        uint64_t padding_index = 0, padding_length = 0;
        *consumed += picoquic_varint_decode(bytes + *consumed, 8, &padding_index);
        *consumed += picoquic_varint_decode(bytes + *consumed, 8, &padding_length);
        if (!skip_repair_payload && padding_index > 0) {
            my_memcpy(rf->symbols[i]->repair_payload, bytes + *consumed, padding_index);
        }
        *consumed += padding_index;
        if (!skip_repair_payload) {
            my_memcpy(rf->symbols[i]->repair_payload + padding_index + padding_length, bytes + *consumed, symbol_size - padding_index - padding_length);
            PROTOOP_PRINTF(cnx, "FIRST BYTES = %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx, %hhx\n", rf->symbols[i]->repair_payload[0],
                           rf->symbols[i]->repair_payload[1], rf->symbols[i]->repair_payload[2],
                           rf->symbols[i]->repair_payload[3], rf->symbols[i]->repair_payload[4],
                           rf->symbols[i]->repair_payload[5], rf->symbols[i]->repair_payload[6], rf->symbols[i]->repair_payload[7],
                           rf->symbols[i]->repair_payload[8], rf->symbols[i]->repair_payload[9]);
        }
        *consumed += symbol_size - padding_index - padding_length;
    }

    return rf;
}


#endif //PICOQUIC_COMPRESSED_REPAIR_FRAME_H
