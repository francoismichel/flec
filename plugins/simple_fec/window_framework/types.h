
#ifndef PICOQUIC_TYPES_H
#define PICOQUIC_TYPES_H

#include <picoquic.h>
#include <memcpy.h>
#include "../utils.h"
#include "../fec.h"

/**
 * recovers the missing symbols from a window
 * \param[in] fec_scheme <b> window_fec_scheme_t </b> the fec scheme state
 * \param[in] source_symbols <b> window_source_symbol_t ** </b> array of source symbols (a symbol is NULL if it is not present)
 * \param[in] n_source_symbols <b> uint16_t </b> size of source_symbols
 * \param[in] repair_symbols <b> window_repair_symbol_t ** </b> array of repair symbols
 * \param[in] n_repair_symbols <b> uint16_t </b> size of repair_symbols
 * \param[in] n_missing_source_symbols <b> uint16_t </b> number of missing source symbols in the array
 * \param[in] symbol_size <b> uint16_t </b> size of a source/repair symbol in bytes
 * \param[in] smallest_source_symbol_id <b> window_source_symbol_id_t </b> the id of the smallest source symbol in the array
 *
 * \return \b int Error code, 0 iff everything was fine
 */
#define FEC_PROTOOP_WINDOW_FEC_SCHEME_RECOVER "window_fecscheme_recover"
#define FEC_PROTOOP_WINDOW_CONTROLLER_SLOT_ACKED "fec_controller_slot_acked"
#define FEC_PROTOOP_WINDOW_CONTROLLER_SLOT_NACKED "fec_controller_slot_nacked"
#define FEC_PROTOOP_WINDOW_CONTROLLER_FREE_SLOT "fec_controller_free_slot"
#define WINDOW_INITIAL_SYMBOL_ID 1

#define MAX_SENDING_WINDOW_SIZE 8000


#define for_each_window_source_symbol(____sss, ____ss, ____nss) \
    for (int ____i = 0, ____keep = 1, n = ____nsss; ____keep && ____i < n; ____i++, ____keep = 1-____keep ) \
        for (____ss = ____sss[____i] ; ____keep ; ____keep = 1-____keep)

#define for_each_window_repair_symbol(____rss, ____rs, ____nrs) \
    for (int ____i = 0, ____keep = 1, n = ____nrs; ____keep && ____i < n; ____i++, ____keep = 1-____keep ) \
        for (____rs = ____rss[____i] ; ____keep ; ____keep = 1-____keep)




#define MAX_WINDOW_SOURCE_SYMBOL_ID UINT32_MAX

typedef uint32_t window_source_symbol_id_t; // it is just a contiguous sequence number

typedef struct fss {
    union {
        uint8_t  val[4];
        uint32_t val_big_endian;
    };
} window_fec_scheme_specific_t; // the fec scheme-specific value is 4 arbitrary bytes

typedef struct repair_symbols_metadata {
    window_fec_scheme_specific_t fss;
    window_source_symbol_id_t first_id;
    uint16_t n_protected_symbols;
} repair_symbols_metadata_t;


typedef struct {
    repair_symbol_t repair_symbol;
    repair_symbols_metadata_t metadata;
} window_repair_symbol_t;


typedef struct {
    source_symbol_t source_symbol;
    window_source_symbol_id_t id;
} window_source_symbol_t;

// packed needed because of the malloc block size...
typedef struct __attribute__((__packed__)) {
    window_source_symbol_id_t first_symbol_id;
    uint16_t number_of_symbols;
} window_source_symbol_packet_metadata_t;

typedef struct __attribute__((__packed__)) {
    window_source_symbol_id_t first_protected_source_symbol_id;
    uint16_t n_protected_source_symbols;
    uint16_t number_of_repair_symbols;
    uint8_t is_fb_fec;

} window_repair_symbol_packet_metadata_t;

typedef struct __attribute__((__packed__)) {
    window_repair_symbol_packet_metadata_t repair_metadata;
    window_source_symbol_packet_metadata_t source_metadata;
} window_packet_metadata_t;


typedef struct {
    uint64_t pn;
    window_source_symbol_id_t id;
    size_t n_symbols;
} window_recovered_range_t;

static __attribute__((always_inline)) window_source_symbol_t *create_window_source_symbol(picoquic_cnx_t *cnx, uint16_t symbol_size) {
    window_source_symbol_t *ss = my_malloc(cnx, MAX(MALLOC_SIZE_FOR_FRAGMENTATION, sizeof(window_source_symbol_t)));
    if (!ss)
        return NULL;
    my_memset(ss, 0, sizeof(window_source_symbol_t));

    ss->source_symbol._whole_data = my_malloc(cnx, MAX(MALLOC_SIZE_FOR_FRAGMENTATION, align(symbol_size*sizeof(uint8_t))));
    if (!ss->source_symbol._whole_data) {
        my_free(cnx, ss);
        return NULL;
    }
    my_memset(ss->source_symbol._whole_data, 0, symbol_size*sizeof(uint8_t));
    ss->source_symbol.chunk_data = &ss->source_symbol._whole_data[1];
    ss->source_symbol.chunk_size = symbol_size - 1;
    return ss;
}

static __attribute__((always_inline)) void delete_window_source_symbol(picoquic_cnx_t *cnx, window_source_symbol_t *ss) {
    delete_source_symbol(cnx, (source_symbol_t *) ss);
}

static __attribute__((always_inline)) window_repair_symbol_t *create_window_repair_symbol(picoquic_cnx_t *cnx, uint16_t symbol_size) {
    window_repair_symbol_t *rs = my_malloc(cnx, sizeof(window_source_symbol_t));
    if (!rs)
        return NULL;
    my_memset(rs, 0, sizeof(window_repair_symbol_t));

    rs->repair_symbol.repair_payload = my_malloc(cnx, align(symbol_size*sizeof(uint8_t)));
    if (!rs->repair_symbol.repair_payload) {
        my_free(cnx, rs);
        return NULL;
    }
    my_memset(rs->repair_symbol.repair_payload, 0, symbol_size*sizeof(uint8_t));
    rs->repair_symbol.payload_length = symbol_size;
    return rs;
}

static __attribute__((always_inline)) void delete_window_repair_symbol(picoquic_cnx_t *cnx, window_repair_symbol_t *rs) {
    delete_repair_symbol(cnx, (repair_symbol_t *) rs);
}

typedef struct fec_src_fpi_frame {
    source_symbol_id_t  id;
} _fec_src_fpi_frame_t;

typedef struct fec_repair_frame {
    repair_symbol_t symbol;
} _fec_repair_frame_t;

typedef protoop_arg_t window_redundancy_controller_t;


typedef struct window_repair_frame {
    bool is_fb_fec;
    window_fec_scheme_specific_t fss;
    window_source_symbol_id_t first_protected_symbol;
    uint16_t n_protected_symbols;
    uint16_t n_repair_symbols;
    repair_symbol_t **symbols;
} window_repair_frame_t;


typedef struct window_rwin_frame {
    window_source_symbol_id_t smallest_id;
    int64_t window_size;
} window_rwin_frame_t;

static __attribute__((always_inline)) window_repair_frame_t *create_repair_frame_without_symbols(picoquic_cnx_t *cnx) {
    window_repair_frame_t *rf = my_malloc(cnx, sizeof(window_repair_frame_t));
    if (!rf)
        return NULL;
    my_memset(rf, 0, sizeof(window_repair_frame_t));
    return rf;
}

// if not null, will free rf->symbols but not the symbols themselves
static __attribute__((always_inline)) window_repair_frame_t *delete_repair_frame_symbols(picoquic_cnx_t *cnx, window_repair_frame_t *rf) {
    if (rf->symbols) {
        for (int i = 0 ; i < rf->n_repair_symbols ; i++) {
            if (rf->symbols[i]) {
                delete_repair_symbol(cnx, rf->symbols[i]);
            }
        }
        my_free(cnx, rf->symbols);
        rf->symbols = NULL;
    }
    return rf;
}

// if not null, will free rf->symbols but not the symbols themselves
static __attribute__((always_inline)) void delete_repair_frame(picoquic_cnx_t *cnx, window_repair_frame_t *rf) {
    PROTOOP_PRINTF(cnx, "DELETE REPAIR FRAME\n");
    delete_repair_frame_symbols(cnx, rf);
    my_free(cnx, rf);
}

static __attribute__((always_inline)) window_rwin_frame_t *create_window_rwin_frame(picoquic_cnx_t *cnx) {
    window_rwin_frame_t *rwin_frame = my_malloc(cnx, sizeof(window_rwin_frame_t));
    if (!rwin_frame)
        return NULL;
    my_memset(rwin_frame, 0, sizeof(window_rwin_frame_t));
    return rwin_frame;
}

static __attribute__((always_inline)) void delete_window_rwin_frame(picoquic_cnx_t *cnx, window_rwin_frame_t *frame) {
    my_free(cnx, frame);
}

#define REPAIR_FRAME_HEADER_SIZE (member_size(window_fec_scheme_specific_t, val) + member_size(window_repair_frame_t, first_protected_symbol) + member_size(window_repair_frame_t, n_protected_symbols) + \
                                  member_size(window_repair_frame_t, n_repair_symbols))

static __attribute__((always_inline)) int serialize_window_source_symbol_id(uint8_t *out_buffer, size_t buffer_length, window_source_symbol_id_t id, size_t *consumed) {
    // TODO: remove this below and make it more generic
    if (buffer_length < sizeof(id) || sizeof(id) != 4)
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    id &= 0x3FFFFFFF;
    id |= 0x80000000;
    encode_un(id, out_buffer, sizeof(id));
    *consumed = sizeof(id);
    return 0;
}


static __attribute__((always_inline)) int serialize_window_repair_frame(picoquic_cnx_t *cnx, uint8_t *out_buffer, size_t buffer_length, window_repair_frame_t *repair_frame, uint16_t symbol_size, size_t *consumed) {
    if (buffer_length < REPAIR_FRAME_HEADER_SIZE + repair_frame->n_repair_symbols*symbol_size)
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    *consumed = 0;
    // encode fec-scheme-specific
    // 4 bytes
    my_memcpy(out_buffer, repair_frame->fss.val, sizeof(repair_frame->fss.val));
    *consumed += sizeof(repair_frame->fss.val);
    size_t tmp = 0;
    // encode symbol id
    // 4 bytes
    int err = serialize_window_source_symbol_id(out_buffer + *consumed, buffer_length, repair_frame->first_protected_symbol, &tmp);
    if (err)
        return err;
    *consumed += tmp;
    // encode number of repair symbols (the symbol size is implicitly negociated so we don't need to encode it)
    // 2 bytes
    encode_un(repair_frame->n_protected_symbols, out_buffer + *consumed, sizeof(repair_frame->n_protected_symbols));
    *consumed += sizeof(repair_frame->n_protected_symbols);
    // 2 bytes
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
        my_memcpy(out_buffer + *consumed, repair_frame->symbols[i]->repair_payload, symbol_size);
        *consumed += symbol_size;

    }
    return 0;
}

static __attribute__((always_inline)) int decode_window_source_symbol_id(uint8_t *buffer, size_t buffer_length,
                                                                         window_source_symbol_id_t *id, size_t *consumed) {
    if (buffer_length < sizeof(window_source_symbol_id_t))
        return PICOQUIC_ERROR_MEMORY;
    // TODO: remove this below and make it generic by using redular varints
    // little hack it is encoded as a varint but it always takes 4 bytes
    *id = decode_u32(buffer);
    *id &= 0x3FFFFFFF;
    *consumed = sizeof(window_source_symbol_id_t);


    return 0;
}

static __attribute__((always_inline)) int serialize_window_fpi_frame(uint8_t *out_buffer, size_t buffer_length, source_symbol_id_t ssid, size_t *consumed, size_t symbol_size) {
    window_source_symbol_id_t id = (window_source_symbol_id_t) ssid;
    return serialize_window_source_symbol_id(out_buffer, buffer_length, id, consumed);
}

static __attribute__((always_inline)) window_repair_frame_t *parse_window_repair_frame(picoquic_cnx_t *cnx, uint8_t *bytes, const uint8_t *bytes_max,
        uint16_t symbol_size, size_t *consumed, bool skip_repair_payload) {
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

    if (bytes_max - bytes - *consumed < rf->n_repair_symbols*symbol_size)
        return NULL;
    if (!skip_repair_payload) {
        PROTOOP_PRINTF(cnx, "ENTER IN IF, N_RS = %d\n", rf->n_repair_symbols);
        rf->symbols = my_malloc(cnx, rf->n_repair_symbols*sizeof(window_repair_symbol_t *));
        if (!rf->symbols) {
            delete_repair_frame(cnx, rf);
            *consumed = 0;
            return NULL;
        }
        my_memset(rf->symbols, 0, rf->n_repair_symbols*sizeof(window_repair_symbol_t *));
        // decode payload
        for (int i = 0 ; i < rf->n_repair_symbols ; i++) {
            // decoding the ith symbol
            window_repair_symbol_t *rs = create_window_repair_symbol(cnx, symbol_size);
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

            my_memcpy(rf->symbols[i]->repair_payload, bytes + *consumed, symbol_size);
            *consumed += symbol_size;
        }

    } else {
        // skip the payload, because of skip_frame...
        *consumed += rf->n_repair_symbols*symbol_size;
    }
    PROTOOP_PRINTF(cnx, "RETURN\n");
    return rf;
}

static __attribute__((always_inline)) size_t varint_len(uint64_t val) {
    if (val <= 63) {
        return 1;
    } else if (val <= 16383) {
        return 2;
    } else if (val <= 1073741823) {
        return 4;
    } else if (val <= 4611686018427387903) {
        return 8;
    }
    return 0;
}

static __attribute__((always_inline)) int parse_window_rwin_frame(uint8_t *buffer, size_t buffer_length,
                                                                         window_rwin_frame_t *frame, size_t *consumed) {
    if (buffer_length == 0)
        return PICOQUIC_ERROR_MEMORY;
    *consumed = 0;
    uint64_t decoded = 0;
    *consumed += picoquic_varint_decode(buffer + *consumed, buffer_length - *consumed, &decoded);
    if (decoded > MAX_WINDOW_SOURCE_SYMBOL_ID) {
        return -1;
    }
    frame->smallest_id = (window_source_symbol_id_t) decoded;
    if (buffer_length - *consumed == 0)
        return PICOQUIC_ERROR_MEMORY;
    *consumed += picoquic_varint_decode(buffer + *consumed, buffer_length - *consumed, &decoded);
    frame->window_size = decoded;
    if (frame->window_size < 0) {
        return -2;
    }
    return 0;
}

static __attribute__((always_inline)) int serialize_window_rwin_frame(uint8_t *out_buffer, size_t buffer_length, window_rwin_frame_t *frame, size_t *consumed) {
    *consumed = 0;
    if (buffer_length - *consumed < varint_len(frame->smallest_id)) {
        return PICOQUIC_ERROR_MEMORY;
    }
    *consumed += picoquic_varint_encode(out_buffer + *consumed, buffer_length - *consumed, frame->smallest_id);
    if (buffer_length - *consumed < varint_len(frame->window_size)) {
        return PICOQUIC_ERROR_MEMORY;
    }
    *consumed += picoquic_varint_encode(out_buffer + *consumed, buffer_length - *consumed, frame->window_size);
    return 0;
}

typedef struct recovered_frame {
    uint64_t *packet_numbers;
    window_source_symbol_id_t *ids;
    size_t n_packets;
    size_t max_packets;
} window_recovered_frame_t;

static __attribute__((always_inline)) window_recovered_frame_t *create_window_recovered_frame(picoquic_cnx_t *cnx) {
    window_recovered_frame_t *rf = my_malloc(cnx, sizeof(window_recovered_frame_t));
    if (!rf)
        return NULL;
    my_memset(rf, 0, sizeof(window_recovered_frame_t));
    rf->max_packets = 1000;
    rf->packet_numbers = my_malloc(cnx, rf->max_packets*sizeof(rf->packet_numbers[0]));
    if (!rf->packet_numbers) {
        my_free(cnx, rf);
        return NULL;
    }
    rf->ids = my_malloc(cnx, rf->max_packets*sizeof(rf->ids[0]));
    if (!rf->ids) {
        my_free(cnx, rf->packet_numbers);
        my_free(cnx, rf);
        return NULL;
    }
    my_memset(rf->packet_numbers, 0, rf->max_packets*sizeof(rf->packet_numbers[0]));
    my_memset(rf->ids, 0, rf->max_packets*sizeof(rf->ids[0]));
    rf->n_packets = 0;
    return rf;
}

static __attribute__((always_inline)) void delete_recovered_frame(picoquic_cnx_t *cnx, window_recovered_frame_t *rf) {
    my_free(cnx, rf->packet_numbers);
    my_free(cnx, rf->ids);
    my_free(cnx, rf);
}

static __attribute__((always_inline)) bool add_packet_to_recovered_frame(picoquic_cnx_t *cnx, window_recovered_frame_t *rf, uint64_t packet_number, window_source_symbol_id_t id) {
    if (rf->n_packets == rf->max_packets)
        return false;
    rf->packet_numbers[rf->n_packets] = packet_number;
    rf->ids[rf->n_packets] = id;
    rf->n_packets++;
    return true;
}

static __attribute__((always_inline)) void print_source_symbol(picoquic_cnx_t *cnx, window_source_symbol_t *ss) {
    PROTOOP_PRINTF(cnx, "PRINT SYMBOL %u\n", ss->id);
    for (int i = 0 ; i < 9 ; i++) {
        PROTOOP_PRINTF(cnx, "0x%x, ", ss->source_symbol._whole_data[i]);
    }
    PROTOOP_PRINTF(cnx, "... ");
    for (int i = ss->source_symbol.chunk_size - 3 ; i < ss->source_symbol.chunk_size + 1 ; i++) {
        PROTOOP_PRINTF(cnx, "0x%x, ", ss->source_symbol._whole_data[i]);
    }
    PROTOOP_PRINTF(cnx, "DONE\n");
}

static __attribute__((always_inline)) void print_source_symbol_payload(picoquic_cnx_t *cnx, uint8_t *data, size_t size) {
    PROTOOP_PRINTF(cnx, "PRINT SYMBOL PAYLOAD\n");
    for (int i = 0 ; i < 9 ; i++) {
        PROTOOP_PRINTF(cnx, "0x%x, ", data[i]);
    }
    PROTOOP_PRINTF(cnx, "... ");
    for (int i = size - 4 ; i < size ; i++) {
        PROTOOP_PRINTF(cnx, "0x%x, ", data[i]);
    }
    PROTOOP_PRINTF(cnx, "DONE\n");
}

static __attribute__((always_inline)) int serialize_packet_ranges(picoquic_cnx_t *cnx, uint8_t *bytes, size_t buffer_length, uint64_t *packet_numbers, uint64_t n_packets, size_t *consumed) {
    // the packets in rp must be sorted according to their packet number
    ssize_t size = 0;

    size = picoquic_varint_encode(bytes, buffer_length - *consumed, n_packets);
    bytes += size;
    *consumed += size;
    // TODO: encode it as a normal varint and not a varint with a size of always 8 bytes
    uint64_t packet_to_encode = packet_numbers[0] | 0xC000000000000000;
    encode_u64(packet_to_encode, bytes);
    bytes += sizeof(uint64_t);
    *consumed += sizeof(uint64_t);
    PROTOOP_PRINTF(cnx, "FIRST NUMBER = %lu\n", packet_numbers[0]);
    uint64_t range_length = 0;
    for (int i = 1 ; i < n_packets ; i++) {
        if (packet_numbers[i] <= packet_numbers[i-1]) {
            // error
            *consumed = 0;
            PROTOOP_PRINTF(cnx, "ERROR: THE PACKETS ARE NOT IN ORDER IN THE RECOVERED FRAME: %lx <= %lx\n", packet_numbers[i], packet_numbers[i-1]);
            return -1;
        }
        if (packet_numbers[i] == packet_numbers[i-1] + 1 || packet_numbers[i] == packet_numbers[i-1]) {
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
            PROTOOP_PRINTF(cnx, "WRITE STREAK %lu\n", range_length);
            bytes += size;
            *consumed += size;
            // write gap
            size = picoquic_varint_encode(bytes, buffer_length - *consumed, (packet_numbers[i] - packet_numbers[i-1]) - 1);
            PROTOOP_PRINTF(cnx, "WRITE GAP %lu\n", (packet_numbers[i] - packet_numbers[i-1]) - 1);
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
    return 0;
}

static __attribute__((always_inline)) int serialize_id_ranges(picoquic_cnx_t *cnx, uint8_t *bytes, size_t buffer_length, window_recovered_frame_t *rf, size_t *consumed) {
    // quick ack: put the ids in a uint64_t array so that we can encode them the same way as the packet numbers
    uint64_t *ids = my_malloc(cnx, rf->n_packets*sizeof(uint64_t));
    if (!ids) {
        return PICOQUIC_ERROR_MEMORY;
    }
    for (int i = 0 ; i < rf->n_packets ; i++) {
        ids[i] = (uint64_t) rf->ids[i];
    }
    int err = serialize_packet_ranges(cnx, bytes, buffer_length, ids, rf->n_packets, consumed);
    my_free(cnx, ids);
    return err;
}


// we do not write the type byte
static __attribute__((always_inline)) int serialize_window_recovered_frame(picoquic_cnx_t *cnx, uint8_t *bytes, size_t buffer_length, window_recovered_frame_t *rf, size_t *consumed) {
    *consumed = 0;

    int err = serialize_packet_ranges(cnx, bytes, buffer_length, rf->packet_numbers, rf->n_packets, consumed);

    if (err) {
        PROTOOP_PRINTF(cnx, "ERROR WHILE SERIALIZING RECOVERED FRAME PACKET RANGE");
        return err;
    }

    uint64_t consumed_packets = *consumed;

    PROTOOP_PRINTF(cnx, "WRITTEN PACKETS RANGE, CONSUMED = %lu\n", consumed_packets);
    *consumed = 0;

    bytes += consumed_packets;
    buffer_length -= consumed_packets;
    err = serialize_id_ranges(cnx, bytes, buffer_length, rf, consumed);

    *consumed += consumed_packets;
    PROTOOP_PRINTF(cnx, "WRITTEN IDS RANGE, TOTAL CONSUMED = %lu, LARGEST RECOVERED PACKET = %lx\n", *consumed, rf->packet_numbers[rf->n_packets-1]);

    return err;


}

// also allocates the needed space for window source symbols ids to be appended after the packet numbers
static __attribute__((always_inline)) uint8_t *parse_packets_range(picoquic_cnx_t *cnx, uint8_t *bytes, uint8_t *bytes_max, size_t *consumed, size_t *allocated_size) {
    uint64_t first_recovered_packet;
    uint8_t *bytes_orig = bytes;
    uint64_t number_of_packets;
    ssize_t size = picoquic_varint_decode(bytes, bytes_max - bytes, &number_of_packets);
    bytes += size;

    // TODO: currently encoded as a varing but always on 8 bytes, we need to change it to a regular varint
    first_recovered_packet = decode_u64(bytes);
    first_recovered_packet &= 0x3FFFFFFFFFFFFFFF;
    bytes += sizeof(uint64_t);
    *allocated_size = sizeof(uint64_t) + number_of_packets*(sizeof(window_recovered_range_t));  // sadly, we must place everything in one single malloc, because skip_frame will free our output
    uint8_t *size_and_ranges = my_malloc(cnx, *allocated_size);
    if (!size)
        return NULL;
    my_memset(size_and_ranges, 0, *allocated_size);
    window_recovered_range_t *ranges = (window_recovered_range_t *) &(((uint64_t *) size_and_ranges)[1]);

    ranges[0].pn = first_recovered_packet;
    ranges[0].n_symbols = 1;

    int currently_parsed_recovered_packets = 1;
    uint64_t next_packet_to_check = first_recovered_packet + 1;
    bool range_is_gap = false;
    size_t n_ranges = 0;
//    PROTOOP_PRINTF(cnx, "FIRST PACKET = %lu\n", ranges[0].pn);
    while(currently_parsed_recovered_packets < number_of_packets && bytes < bytes_max) {
        uint64_t range_size;
        size = picoquic_varint_decode(bytes, bytes_max - bytes, &range_size);
        bytes += size;
        if (!range_is_gap) {
//            PROTOOP_PRINTF(cnx, "STREAK SIZE = %lu\n", range_size);
            // this is a range of recovered packets
            if (currently_parsed_recovered_packets + range_size > number_of_packets) {
                PROTOOP_PRINTF(cnx, "ERROR PARSING RECOVERED FRAME: THE RANGE GOES OUT OF THE PACKET: %d + %lu > %lu\n", currently_parsed_recovered_packets, range_size, number_of_packets);
                // error
                my_free(cnx, size_and_ranges);
                return NULL;
            }
            if (ranges[n_ranges].n_symbols == 0) {
                // new range
                ranges[n_ranges].pn = next_packet_to_check;
                ranges[n_ranges].n_symbols = range_size;
                next_packet_to_check += range_size;
            } else {
                // update range
                ranges[n_ranges].n_symbols += range_size;
                next_packet_to_check += range_size;
            }
//            PROTOOP_PRINTF(cnx, "ranges[%d].pn = %lu, n_ranges\n", n_ranges, ranges[n_ranges].pn, n_ranges);
            currently_parsed_recovered_packets += range_size;
            n_ranges++;

            range_is_gap = true; // after a range of recovered packets, there must be a gap or nothing
        } else {
//            PROTOOP_PRINTF(cnx, "GAP SIZE = %lu\n", range_size);
            // this range is a gap of recovered packets
            // it implicitly announces the recovery of the packet just after this gap
            next_packet_to_check += range_size;
            ranges[n_ranges].pn = next_packet_to_check;
            ranges[n_ranges].n_symbols = 1;
            next_packet_to_check++;
            currently_parsed_recovered_packets++;
            range_is_gap = false; // after a gap of recovered packets, there must be a range or nothing
//            PROTOOP_PRINTF(cnx, "PACKET %lx HAS BEEN RECOVERED BY THE PEER\n", last_recovered_packet);
        }
    }
    if (!range_is_gap) {
        // means that we parsed a gap just before
        n_ranges++;
    }
    ((uint64_t *) size_and_ranges)[0] = n_ranges;

    if (currently_parsed_recovered_packets != number_of_packets || bytes > bytes_max) {
        // error
        my_free(cnx, size_and_ranges);
        PROTOOP_PRINTF(cnx, "DID NOT PARSE THE CORRECT NUMBER OF RECOVERED PACKETS (%u < %u)\n", currently_parsed_recovered_packets, number_of_packets);
        return NULL;
    }
    *consumed = bytes - bytes_orig;
    return size_and_ranges;
}

static __attribute__((always_inline)) int parse_ids_range(picoquic_cnx_t *cnx, uint8_t *bytes, uint8_t *bytes_max, size_t *consumed, window_recovered_range_t *ranges, int n_max_ranges) {
    size_t tmp = 0;
    // the packets range format is the same as the IDs ranges so we use a little hack here
    uint8_t *size_and_ids_ranges = parse_packets_range(cnx, bytes, bytes_max, consumed, &tmp);
    if (!size_and_ids_ranges) {
        return PICOQUIC_ERROR_MEMORY;
    }
    size_t n_ranges = ((uint64_t *) size_and_ids_ranges)[0];
    if (n_ranges > n_max_ranges) {
        my_free(cnx, size_and_ids_ranges);
        return PICOQUIC_ERROR_MEMORY;
    }
    window_recovered_range_t *ranges64 = (window_recovered_range_t *) &(((uint64_t *) size_and_ids_ranges)[1]);
    int64_t next_range_start = -1L;
    int n_remaining_numbers_in_range = 0;
    if (n_ranges > 0) {
        next_range_start = ranges64[0].pn;
        n_remaining_numbers_in_range = ranges64[0].n_symbols;
    }
    int current_range64_idx = 0;
    for (int i = 0 ; current_range64_idx < n_ranges ; i++) {
//        PROTOOP_PRINTF(cnx, "SET RANGE ID TO %u (PACKET = %lx, LENGTH = %lu)\n", ranges64[current_range64_idx].pn, ranges[i].pn, ranges[i].n_symbols);
        ranges[i].id = (window_source_symbol_id_t) next_range_start;
        // we never have a n_remaining_numbers_in_range that is smaller than ranges[i].n_symbols because when packets are contiguous, IDs are contiguous
        next_range_start = ranges64[current_range64_idx].pn + ranges[i].n_symbols;
        n_remaining_numbers_in_range = n_remaining_numbers_in_range - ranges[i].n_symbols;
        if (n_remaining_numbers_in_range == 0) {
            current_range64_idx++;
            if (current_range64_idx < n_ranges) {
                next_range_start = ranges64[current_range64_idx].pn;
                n_remaining_numbers_in_range = ranges64[current_range64_idx].n_symbols;
            }
        }
    }
    my_free(cnx, size_and_ids_ranges);
    return 0;
}

// we do not read the type byte
static __attribute__((always_inline)) uint8_t *parse_window_recovered_frame(picoquic_cnx_t *cnx, uint8_t *bytes, uint8_t *bytes_max, size_t *consumed) {
    PROTOOP_PRINTF(cnx, "PARSE RECOVERED FRAME, AVAILABLE SPACE = %ld\n", bytes_max - bytes);

    uint64_t now = picoquic_current_time();
    size_t allocated_size = 0;
    uint8_t *size_and_ranges = parse_packets_range(cnx, bytes, bytes_max, consumed, &allocated_size);
    if (!size_and_ranges) {
        return NULL;
    }
    PROTOOP_PRINTF(cnx, "PARSED PACKETS, CONSUMED = %lu\n", *consumed);
    uint64_t n_ranges = (((uint64_t *) size_and_ranges)[0]);
    window_recovered_range_t *ranges = (window_recovered_range_t *) &(((uint64_t *) size_and_ranges)[1]);

    size_t consumed_packets = *consumed;

    *consumed = 0;
    bytes += consumed_packets;

    size_t n_max_ids_ranges = n_ranges;

    if (n_max_ids_ranges != n_ranges) {
        // error
        my_free(cnx, size_and_ranges);
        PROTOOP_PRINTF(cnx, "ERROR: PARSING RECOVERED FRAME: INVALID NUMBER OF IDS TO PARSED GIVEN THE SIZE OF THE OUTPUT BUFFER: %lu != %lu\n",
                       n_max_ids_ranges, n_ranges);
        return NULL;
    }
    PROTOOP_PRINTF(cnx, "PARSING IDS\n");

    int err = parse_ids_range(cnx, bytes, bytes_max, consumed, ranges, n_max_ids_ranges);

    if (err) {
        my_free(cnx, size_and_ranges);
        return NULL;
    }
    *consumed += consumed_packets;
    PROTOOP_PRINTF(cnx, "PARSED IDS, CONSUMED = %lu, %lu RANGES, elapsed = %uµs\n", *consumed, n_ranges, picoquic_current_time() - now);

    return size_and_ranges;
}

#define WINDOW_FEC_SCHEME_RECEIVE_SOURCE_SYMBOL "win_fs_recv_ss"
static __attribute__((always_inline)) int window_fec_scheme_receive_source_symbol(picoquic_cnx_t *cnx, fec_scheme_t fec_scheme, window_source_symbol_t *ss, void **removed, int *used_in_system) {
    protoop_arg_t args[2];
    args[0] = (protoop_arg_t) fec_scheme;
    args[1] = (protoop_arg_t) ss;
    protoop_arg_t output[2];
    int retval = (int) run_noparam(cnx, WINDOW_FEC_SCHEME_RECEIVE_SOURCE_SYMBOL, 2, args, output);
    *removed = (void *) output[0];
    *used_in_system = output[1];
    return retval;
}


#define WINDOW_FEC_SCHEME_RECEIVE_REPAIR_SYMBOL "win_fs_recv_rs"
static __attribute__((always_inline)) int fec_scheme_receive_repair_symbol(picoquic_cnx_t *cnx, fec_scheme_t fec_scheme, window_repair_symbol_t *rs, void **removed, int *used_in_system) {
    protoop_arg_t args[2];
    args[0] = (protoop_arg_t) fec_scheme;
    args[1] = (protoop_arg_t) rs;
    protoop_arg_t output[2];
    int retval = (int) run_noparam(cnx, WINDOW_FEC_SCHEME_RECEIVE_REPAIR_SYMBOL, 2, args, output);
    *removed = (void *) output[0];
    *used_in_system = output[1];
    return retval;
}

#define WINDOW_FEC_SCHEME_REMOVE_UNUSED_REPAIR_SYMBOLS "win_fs_rmv_rs"
static __attribute__((always_inline)) int fec_scheme_remove_unused_repair_symbols(picoquic_cnx_t *cnx, fec_scheme_t fec_scheme, window_source_symbol_id_t highest_contiguously_received) {
    protoop_arg_t args[2];
    args[0] = (protoop_arg_t) fec_scheme;
    args[1] = (protoop_arg_t) highest_contiguously_received;
    int retval = (int) run_noparam(cnx, WINDOW_FEC_SCHEME_REMOVE_UNUSED_REPAIR_SYMBOLS, 2, args, NULL);
    return retval;
}

#define WINDOW_FEC_SCHEME_SET_MAXIMUM_NUMBER_OF_REPAIR_SYMBOLS "win_fs_set_max_rs"
static __attribute__((always_inline)) int fec_scheme_set_max_rs(picoquic_cnx_t *cnx, fec_scheme_t fec_scheme, uint64_t max) {
    protoop_arg_t args[2];
    args[0] = (protoop_arg_t) fec_scheme;
    args[1] = (protoop_arg_t) max;
    int retval = (int) run_noparam(cnx, WINDOW_FEC_SCHEME_SET_MAXIMUM_NUMBER_OF_REPAIR_SYMBOLS, 2, args, NULL);
    return retval;
}


#endif //PICOQUIC_TYPES_H
