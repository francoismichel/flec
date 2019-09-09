
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
#define WINDOW_INITIAL_SYMBOL_ID 1



#define for_each_window_source_symbol(____sss, ____ss, ____nss) \
    for (int ____i = 0, ____keep = 1, n = ____nsss; ____keep && ____i < n; ____i++, ____keep = 1-____keep ) \
        for (____ss = ____sss[____i] ; ____keep ; ____keep = 1-____keep)

#define for_each_window_repair_symbol(____rss, ____rs, ____nrs) \
    for (int ____i = 0, ____keep = 1, n = ____nrs; ____keep && ____i < n; ____i++, ____keep = 1-____keep ) \
        for (____rs = ____rss[____i] ; ____keep ; ____keep = 1-____keep)





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

static __attribute__((always_inline)) window_source_symbol_t *create_window_source_symbol(picoquic_cnx_t *cnx, uint16_t symbol_size) {
    window_source_symbol_t *ss = my_malloc(cnx, sizeof(window_source_symbol_t));
    if (!ss)
        return NULL;
    my_memset(ss, 0, sizeof(window_source_symbol_t));

    ss->source_symbol._whole_data = my_malloc(cnx, symbol_size*sizeof(uint8_t));
    if (!ss->source_symbol._whole_data) {
        my_free(cnx, ss);
        return NULL;
    }
    my_memset(ss->source_symbol._whole_data, 0, symbol_size*sizeof(uint8_t));
    ss->source_symbol.chunk_data = &ss->source_symbol._whole_data[1];
    ss->source_symbol.chunk_size = symbol_size - 1;
    return ss;
}

typedef struct fec_src_fpi_frame {
    source_symbol_id_t  id;
} _fec_src_fpi_frame_t;

typedef struct fec_repair_frame {
    repair_symbol_t symbol;
} _fec_repair_frame_t;

typedef _fec_src_fpi_frame_t * fec_src_fpi_frame_t;
typedef _fec_repair_frame_t * fec_repair_frame_t;


typedef protoop_arg_t window_redundancy_controller_t;


typedef struct repair_frame {
    window_fec_scheme_specific_t fss;
    window_source_symbol_id_t first_protected_symbol;
    uint16_t n_protected_symbols;
    uint16_t n_repair_symbols;
    repair_symbol_t **symbols;
} window_repair_frame_t;

static __attribute__((always_inline)) window_repair_frame_t *create_repair_frame(picoquic_cnx_t *cnx) {
    window_repair_frame_t *rf = my_malloc(cnx, sizeof(window_repair_frame_t));
    if (!rf)
        return NULL;
    my_memset(rf, 0, sizeof(window_repair_frame_t));
    return rf;
}

// if not null, will free rf->symbols but not the symbols themselves
static __attribute__((always_inline)) window_repair_frame_t *delete_repair_frame(picoquic_cnx_t *cnx, window_repair_frame_t *rf) {
    if (rf->symbols)
        my_free(cnx, rf->symbols);
    my_free(cnx, rf);
    return rf;
}

#define REPAIR_FRAME_HEADER_SIZE (member_size(window_fec_scheme_specific_t, val) + member_size(window_repair_frame_t, first_protected_symbol) + member_size(window_repair_frame_t, n_protected_symbols) + \
                                  member_size(window_repair_frame_t, n_repair_symbols))

static __attribute__((always_inline)) int serialize_window_source_symbol_id(uint8_t *out_buffer, size_t buffer_length, window_source_symbol_id_t id, size_t *consumed) {
    if (buffer_length < sizeof(id))
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    encode_un(id, out_buffer, sizeof(id));
    *consumed = sizeof(id);
    return 0;
}

static __attribute__((always_inline)) int serialize_window_fpi_frame(uint8_t *out_buffer, size_t buffer_length, source_symbol_id_t ssid, size_t *consumed) {
    window_source_symbol_id_t id = (window_source_symbol_id_t) ssid;
    return serialize_window_source_symbol_id(out_buffer, buffer_length, id, consumed);
}

static __attribute__((always_inline)) int serialize_window_repair_frame(picoquic_cnx_t *cnx, uint8_t *out_buffer, size_t buffer_length, window_repair_frame_t *repair_frame, uint16_t symbol_size, size_t *consumed) {
    if (buffer_length < REPAIR_FRAME_HEADER_SIZE + repair_frame->n_repair_symbols*symbol_size)
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
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
    encode_un(repair_frame->n_repair_symbols, out_buffer + *consumed, sizeof(repair_frame->n_repair_symbols));
    *consumed += sizeof(repair_frame->n_repair_symbols);
    // encode payload
    for (int i = 0 ; i < repair_frame->n_repair_symbols ; i++) {
        // FIXME: maybe should we remove the symbol size field from the repair symbols
        if (repair_frame->symbols[i]->payload_length != symbol_size) {
            PROTOOP_PRINTF(cnx, "ERROR: INCONSISTENT REPAIR SYMBOL SIZE");
            return -1;
        }
        // encoding the ith symbol
        my_memcpy(out_buffer + *consumed, repair_frame->symbols[i]->repair_payload, symbol_size);
        *consumed += symbol_size;

    }
    return 0;
}


typedef struct recovered_frame {
    uint64_t *packet_numbers;
    window_source_symbol_id_t *ids;
    size_t n_packets;
    size_t max_packets;
} window_recovered_frame_t;

static __attribute__((always_inline)) window_recovered_frame_t *create_recovered_frame(picoquic_cnx_t *cnx) {
    window_recovered_frame_t *rf = my_malloc(cnx, sizeof(window_recovered_frame_t));
    if (!rf)
        return NULL;
    my_memset(rf, 0, sizeof(window_recovered_frame_t));
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
    rf->max_packets = MIN(PICOQUIC_MAX_PACKET_SIZE/sizeof(uint64_t), PICOQUIC_MAX_PACKET_SIZE/sizeof(window_source_symbol_id_t));
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


// we do not write the type byte
static __attribute__((always_inline)) int serialize_window_recovered_frame(picoquic_cnx_t *cnx, uint8_t *bytes, size_t buffer_length, window_recovered_frame_t *rf, size_t *consumed) {
    *consumed = 0;
    //  frame header                           frame payload
    if (sizeof(uint8_t) + sizeof(uint64_t) + rf->n_packets*(sizeof(uint64_t) + sizeof(window_source_symbol_id_t)) > buffer_length) {
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

#endif //PICOQUIC_TYPES_H
