
#ifndef PICOQUIC_TYPES_H
#define PICOQUIC_TYPES_H

#include <picoquic.h>
#include <memcpy.h>
#include "../utils.h"



typedef struct fec_src_fpi_frame {
    source_symbol_id_t  id;
} _fec_src_fpi_frame_t;

typedef struct fec_repair_frame {
    repair_symbol_t symbol;
} _fec_repair_frame_t;

typedef _fec_src_fpi_frame_t * fec_src_fpi_frame_t;
typedef _fec_repair_frame_t * fec_repair_frame_t;


typedef protoop_arg_t window_redundancy_controller_t;

typedef uint32_t window_source_symbol_id_t; // it is just a contiguous sequence number

typedef struct fss {
    union {
        uint8_t  val[4];
        uint32_t val_big_endian;
    };
} window_fec_scheme_specific_t; // the fec scheme-specific value is 4 arbitrary bytes

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

#endif //PICOQUIC_TYPES_H
