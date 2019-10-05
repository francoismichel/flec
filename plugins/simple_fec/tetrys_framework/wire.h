//
// Created by michelfra on 2/10/19.
//

#ifndef PICOQUIC_FEC_TETRYS_WIRE_H
#define PICOQUIC_FEC_TETRYS_WIRE_H

#include "../../helpers.h"
#include "tetrys_framework.h"

typedef struct tetrys_repair_frame {
    bool is_fb_fec;
    uint16_t n_repair_symbols;
    repair_symbol_t **symbols;
} tetrys_repair_frame_t;


static __attribute__((always_inline)) tetrys_repair_frame_t *create_tetrys_repair_frame_without_symbols(picoquic_cnx_t *cnx) {
    tetrys_repair_frame_t *rf = my_malloc(cnx, sizeof(tetrys_repair_frame_t));
    if (!rf)
        return NULL;
    my_memset(rf, 0, sizeof(tetrys_repair_frame_t));
    return rf;
}

// if not null, will free rf->symbols but not the symbols themselves
static __attribute__((always_inline)) tetrys_repair_frame_t *delete_tetrys_repair_frame_symbols(picoquic_cnx_t *cnx, tetrys_repair_frame_t *rf) {
    if (rf->symbols) {
        my_free(cnx, rf->symbols);
        rf->symbols = NULL;
    }
    return rf;
}


// if not null, will free rf->symbols but not the symbols themselves
static __attribute__((always_inline)) tetrys_repair_frame_t *delete_tetrys_repair_frame(picoquic_cnx_t *cnx, tetrys_repair_frame_t *rf) {
    delete_tetrys_repair_frame_symbols(cnx, rf);
    my_free(cnx, rf);
    return rf;
}

static __attribute__((always_inline)) int serialize_tetrys_source_symbol_id(uint8_t *out_buffer, size_t buffer_length, tetrys_source_symbol_id_t id, size_t *consumed) {
    if (buffer_length < sizeof(id))
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    encode_un(id, out_buffer, sizeof(id));
    *consumed = sizeof(id);
    return 0;
}

static __attribute__((always_inline)) int decode_tetrys_source_symbol_id(uint8_t *buffer, size_t buffer_length,
                                                                         tetrys_source_symbol_id_t *id, size_t *consumed) {
    if (buffer_length < sizeof(tetrys_source_symbol_id_t))
        return PICOQUIC_ERROR_MEMORY;
    *id = decode_u32(buffer);
    *consumed = sizeof(tetrys_source_symbol_id_t);
    return 0;
}

static __attribute__((always_inline)) int serialize_tetrys_fpi_frame(uint8_t *out_buffer, size_t buffer_length, source_symbol_id_t ssid, size_t *consumed, size_t symbol_size) {
    tetrys_source_symbol_id_t id = (tetrys_source_symbol_id_t) ssid;
    return serialize_tetrys_source_symbol_id(out_buffer, buffer_length, id, consumed);
}

#define TETRYS_REPAIR_FRAME_HEADER_SIZE (member_size(tetrys_repair_frame_t, n_repair_symbols))
static __attribute__((always_inline)) int serialize_tetrys_repair_frame(picoquic_cnx_t *cnx, uint8_t *out_buffer, size_t buffer_length, tetrys_repair_frame_t *repair_frame, uint16_t symbol_size, size_t *consumed) {
    if (buffer_length < TETRYS_REPAIR_FRAME_HEADER_SIZE + repair_frame->n_repair_symbols*symbol_size)
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    PROTOOP_PRINTF(cnx, "SERIALIZE REPAIR FRAME, %d SYMBOLS\n", repair_frame->n_repair_symbols);
    *consumed = 0;
    // encode number of repair symbols (the symbol size is implicitly negociated so we don't need to encode it)
    encode_un(repair_frame->n_repair_symbols, out_buffer + *consumed, sizeof(repair_frame->n_repair_symbols));
    *consumed += sizeof(repair_frame->n_repair_symbols);
    // encode payload
    for (int i = 0 ; i < repair_frame->n_repair_symbols ; i++) {
        if (repair_frame->symbols[i]->payload_length < symbol_size) {
            PROTOOP_PRINTF(cnx, "ERROR: INCONSISTENT REPAIR SYMBOL SIZE, RS TO SMALL: RS LENGTH = %u, SYMBOL SIZE = %u\n", repair_frame->symbols[i]->payload_length, symbol_size);
            return -1;
        }
        PROTOOP_PRINTF(cnx, "WRITE SYMBOL %d OF SIZE %d\n", i, repair_frame->symbols[i]->payload_length);
        // encode the size of the symbol
        encode_un(repair_frame->symbols[i]->payload_length, out_buffer + *consumed, sizeof(repair_frame->symbols[i]->payload_length));
        *consumed += sizeof(repair_frame->symbols[i]->payload_length);
        // encoding the ith symbol
        my_memcpy(out_buffer + *consumed, repair_frame->symbols[i]->repair_payload, repair_frame->symbols[i]->payload_length);
        *consumed += repair_frame->symbols[i]->payload_length;

    }
    return 0;
}



static __attribute__((always_inline)) tetrys_repair_frame_t *parse_tetrys_repair_frame(picoquic_cnx_t *cnx, uint8_t *bytes, const uint8_t *bytes_max, size_t *consumed, bool skip_repair_payload) {
    *consumed = 0;
    tetrys_repair_frame_t *rf = create_tetrys_repair_frame_without_symbols(cnx);
    if (!rf)
        return NULL;

    PROTOOP_PRINTF(cnx, "CREATED REPAIR FRAME\n");
    // encode number of repair symbols (the symbol size is implicitly negociated so we don't need to encode it)
    rf->n_repair_symbols = decode_un(bytes + *consumed, sizeof(rf->n_repair_symbols));
    *consumed += sizeof(rf->n_repair_symbols);
    PROTOOP_PRINTF(cnx, "PARSED N RS\n");


    if (!skip_repair_payload) {
        rf->symbols = my_malloc(cnx, rf->n_repair_symbols*sizeof(tetrys_repair_symbol_t *));
        if (!rf->symbols) {
            delete_tetrys_repair_frame(cnx, rf);
            *consumed = 0;
            return NULL;
        }
        my_memset(rf->symbols, 0, rf->n_repair_symbols*sizeof(tetrys_repair_symbol_t *));
    }

    // decode payload
    for (int i = 0 ; i < rf->n_repair_symbols ; i++) {

        PROTOOP_PRINTF(cnx, "PARSE REPAIR SYMBOL %d\n", i);
        tetrys_repair_symbol_t *rs = NULL;

        uint64_t payload_size = decode_un(bytes + *consumed, sizeof(rf->symbols[i]->payload_length));
        *consumed += sizeof(rf->symbols[i]->payload_length);
        PROTOOP_PRINTF(cnx, "BEFORE FOR LOOP, SKIP = %d\n", skip_repair_payload);
        if (!skip_repair_payload) {
            rs = create_tetrys_repair_symbol(cnx, payload_size);
            rf->symbols[i] = (repair_symbol_t *) rs;
            if (!rf->symbols[i]) {
                for (int j = 0 ; j < i ; j++) {
                    delete_repair_symbol(cnx, rf->symbols[j]);
                }
                delete_tetrys_repair_frame(cnx, rf);
                *consumed = 0;
                break;
            }
            rf->symbols[i]->payload_length = payload_size;
            my_memcpy(rf->symbols[i]->repair_payload, bytes + *consumed, rf->symbols[i]->payload_length);
        }
        *consumed += payload_size;
    }

    return rf;
}



typedef struct recovered_frame {
    uint64_t *packet_numbers;
    tetrys_source_symbol_id_t *ids;
    size_t n_packets;
    size_t max_packets;
} tetrys_recovered_frame_t;

static __attribute__((always_inline)) tetrys_recovered_frame_t *create_tetrys_recovered_frame(picoquic_cnx_t *cnx) {
    tetrys_recovered_frame_t *rf = my_malloc(cnx, sizeof(tetrys_recovered_frame_t));
    if (!rf)
        return NULL;
    my_memset(rf, 0, sizeof(tetrys_recovered_frame_t));
    rf->packet_numbers = my_malloc(cnx, PICOQUIC_MAX_PACKET_SIZE);
    if (!rf->packet_numbers) {
        my_free(cnx, rf);
        return NULL;
    }
    rf->ids = my_malloc(cnx, PICOQUIC_MAX_PACKET_SIZE);
    if (!rf->ids) {
        my_free(cnx, rf->packet_numbers);
        my_free(cnx, rf);
        return NULL;
    }
    my_memset(rf->packet_numbers, 0, PICOQUIC_MAX_PACKET_SIZE);
    my_memset(rf->ids, 0, PICOQUIC_MAX_PACKET_SIZE);
    rf->n_packets = 0;
    rf->max_packets = MIN(PICOQUIC_MAX_PACKET_SIZE/sizeof(uint64_t), PICOQUIC_MAX_PACKET_SIZE/sizeof(tetrys_source_symbol_id_t));
    return rf;
}

static __attribute__((always_inline)) void delete_recovered_frame(picoquic_cnx_t *cnx, tetrys_recovered_frame_t *rf) {
    my_free(cnx, rf->packet_numbers);
    my_free(cnx, rf->ids);
    my_free(cnx, rf);
}

static __attribute__((always_inline)) bool add_packet_to_recovered_frame(picoquic_cnx_t *cnx, tetrys_recovered_frame_t *rf, uint64_t packet_number, tetrys_source_symbol_id_t id) {
    if (rf->n_packets == rf->max_packets)
        return false;
    rf->packet_numbers[rf->n_packets] = packet_number;
    rf->ids[rf->n_packets] = id;
    rf->n_packets++;
    return true;
}


// we do not write the type byte
static __attribute__((always_inline)) int serialize_tetrys_recovered_frame(picoquic_cnx_t *cnx, uint8_t *bytes, size_t buffer_length, tetrys_recovered_frame_t *rf, size_t *consumed) {
    *consumed = 0;
    //  frame header                           frame payload
    if (sizeof(uint8_t) + sizeof(uint64_t) + rf->n_packets*(sizeof(uint64_t) + sizeof(tetrys_source_symbol_id_t)) > buffer_length) {
        // buffer too small
        return PICOQUIC_ERROR_MEMORY;
    }

    // the packets in rp must be sorted according to their packet number
    ssize_t size = 0;

    size = picoquic_varint_encode(bytes, buffer_length - *consumed, rf->n_packets);
    bytes += size;
    *consumed += size;

    encode_u64(rf->packet_numbers[0], bytes);
    bytes += sizeof(uint64_t);
    *consumed += sizeof(uint64_t);

    uint64_t range_length = 0;
    for (int i = 1 ; i < rf->n_packets ; i++) {
        // FIXME: handle gaps of more than 0xFF
        if (rf->packet_numbers[i] <= rf->packet_numbers[i-1]) {
            // error
            *consumed = 0;
            PROTOOP_PRINTF(cnx, "ERROR: THE PACKETS ARE NOT IN ORDER IN THE RECOVERED FRAME\n");
            return -1;
        }
        if (rf->packet_numbers[i] == rf->packet_numbers[i-1]) {
            range_length++;
        } else {
            if (buffer_length - *consumed < 2*sizeof(uint8_t)) {
                set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 0);
                set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
                PROTOOP_PRINTF(cnx, "ERROR TOO FEW AVAILABLE BYTES\n");
                return -1;
            }
            // write range
            size = picoquic_varint_encode(bytes, buffer_length - *consumed, range_length);
            bytes += size;
            *consumed += size;
            // write gap
            size = picoquic_varint_encode(bytes, buffer_length - *consumed, (rf->packet_numbers[i] - rf->packet_numbers[i-1]) - 1);
            bytes += size;
            *consumed += size;
            range_length = 0;
        }
    }
    // write last range if needed
    if (range_length > 0) {
        if (buffer_length - *consumed < 2*sizeof(uint8_t)) {
            *consumed = 0;
            return -1;
        }
        size = picoquic_varint_encode(bytes, buffer_length - *consumed, range_length);
        bytes += size;
        *consumed += size;
    }
    // bulk-encode the sfpids
    for (int i = 0 ; i < rf->n_packets ; i++) {
        encode_u32(rf->ids[i], bytes);
        *consumed += sizeof(uint32_t);
        bytes += sizeof(uint32_t);
    }
    return 0;


}

// we do not read the type byte
static __attribute__((always_inline)) uint8_t *parse_tetrys_recovered_frame(picoquic_cnx_t *cnx, uint8_t *bytes, uint8_t *bytes_max, size_t *consumed) {
    PROTOOP_PRINTF(cnx, "PARSE RECOVERED FRAME, AVAILABLE SPACE = %ld\n", bytes_max - bytes);
    uint64_t first_recovered_packet;
    uint8_t *bytes_orig = bytes;
    uint64_t number_of_packets;
    ssize_t size = picoquic_varint_decode(bytes, bytes_max - bytes, &number_of_packets);
    bytes += size;

    first_recovered_packet = decode_u64(bytes);
    bytes += sizeof(uint64_t);
    uint8_t *size_and_packets = my_malloc(cnx, sizeof(uint64_t) + number_of_packets*(sizeof(uint64_t) + sizeof(tetrys_source_symbol_id_t))); // sadly, we must place everything in one single malloc, because skip_frame will free our output
    if (!size)
        return NULL;
    my_memset(size_and_packets, 0, sizeof(uint64_t) + number_of_packets*(sizeof(uint64_t) + sizeof(tetrys_source_symbol_id_t)));
    ((uint64_t *) size_and_packets)[0] = number_of_packets;
    uint64_t *packets =&(((uint64_t *) size_and_packets)[1]);
    packets[0] = first_recovered_packet;
    int currently_parsed_recovered_packets = 1;
    uint64_t last_recovered_packet = first_recovered_packet;
    bool range_is_gap = false;
    while(currently_parsed_recovered_packets < number_of_packets && bytes < bytes_max - 1) {  // - 1 because there is the number of sfpid afterwards
        uint64_t range;
        size = picoquic_varint_decode(bytes, bytes_max - bytes, &range);
        bytes += size;
        if (!range_is_gap) {
            // this is a range of recovered packets
            if (currently_parsed_recovered_packets + range > number_of_packets) {
                PROTOOP_PRINTF(cnx, "ERROR PARSING RECOVERED FRAME: THE RANGE GOES OUT OF THE PACKET: %d + %lu > %lu\n", currently_parsed_recovered_packets, range, number_of_packets);
                // error
                my_free(cnx, size_and_packets);
                return NULL;
            }
            for (int j = 0 ; j < range ; j++) { // we add each packet of the range in the recovered packets
                last_recovered_packet++;    // the last recovered packet is now this one
                packets[currently_parsed_recovered_packets] = last_recovered_packet;
                PROTOOP_PRINTF(cnx, "PACKET %lx HAS BEEN RECOVERED BY THE PEER\n", last_recovered_packet);
                currently_parsed_recovered_packets++;
            }
            range_is_gap = true; // after a range of recovered packets, there must be a gap or nothing
        } else {
            // this range is a gap of recovered packets
            // it implicitly announces the recovery of the packet just after this gap
            last_recovered_packet += range + 1;
            packets[currently_parsed_recovered_packets] = last_recovered_packet;
            currently_parsed_recovered_packets++;
            range_is_gap = false; // after a gap of recovered packets, there must be a range or nothing
            PROTOOP_PRINTF(cnx, "PACKET %lx HAS BEEN RECOVERED BY THE PEER\n", last_recovered_packet);
        }
    }

    if (currently_parsed_recovered_packets != number_of_packets || bytes >= bytes_max) {
        // error
        my_free(cnx, size_and_packets);
        PROTOOP_PRINTF(cnx, "DID NOT PARSE THE CORRECT NUMBER OF RECOVERED PACKETS (%u < %u)\n", currently_parsed_recovered_packets, number_of_packets);
        return NULL;
    }

    uint8_t currently_parsed_sfpids = 0;
    tetrys_source_symbol_id_t *idx = (tetrys_source_symbol_id_t *) (packets + currently_parsed_recovered_packets);
    while(currently_parsed_sfpids < number_of_packets && bytes < bytes_max) {
        size_t cons = 0;
        decode_tetrys_source_symbol_id(bytes, bytes_max - bytes, &idx[currently_parsed_sfpids++], &cons);
        bytes += cons;
    }

    *consumed = bytes - bytes_orig;

    if (currently_parsed_sfpids != number_of_packets || bytes > bytes_max) {
        // error
        my_free(cnx, size_and_packets);
        PROTOOP_PRINTF(cnx, "DID NOT PARSE THE CORRECT NUMBER OF RECOVERED SFPIDS (%u < %u OR %ld > 0)\n", currently_parsed_sfpids, number_of_packets, bytes - bytes_max);
        return NULL;
    }

    return size_and_packets;
}





#endif //PICOQUIC_FEC_TETRYS_WIRE_H
