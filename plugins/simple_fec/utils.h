
#ifndef FEC_UTILS_H
#define FEC_UTILS_H

#include <stdint.h>
#include <memcpy.h>
#include "wire.h"

static __attribute__((always_inline)) uint64_t decode_un(uint8_t *bytes, int n) {
    uint64_t retval = 0;
    uint8_t buffer[n];
    my_memcpy(buffer, bytes, n);
    int i;
    for (i = 0; i < n ; i++) {
        retval <<= 8;
        retval += buffer[i];
    }
    return retval;
}

static __attribute__((always_inline)) void encode_un(uint64_t to_encode, uint8_t *bytes, int n) {
    uint8_t buffer[n];
    int i;
    for (i = 0; i < n ; i++) {
        buffer[i] = (uint8_t) (to_encode >> 8*(n-i-1));
    }
    my_memcpy(bytes, buffer, n);
}

static __attribute__((always_inline)) uint16_t decode_u16(uint8_t *bytes) {
    return (uint16_t) decode_un(bytes, 2);
}

static __attribute__((always_inline)) uint32_t decode_u32(uint8_t *bytes) {
    return (uint32_t) decode_un(bytes, 4);
}

static __attribute__((always_inline)) uint64_t decode_u64(uint8_t *bytes) {
    return decode_un(bytes, 8);
}

static __attribute__((always_inline)) void encode_u16(uint16_t to_encode, uint8_t *bytes) {
    encode_un(to_encode, bytes, 2);
}

static __attribute__((always_inline)) void encode_u32(uint32_t to_encode, uint8_t *bytes) {
    encode_un(to_encode, bytes, 4);
}

static __attribute__((always_inline)) void encode_u64(uint64_t to_encode, uint8_t *bytes) {
    encode_un(to_encode, bytes, 8);
}

static __attribute__((always_inline)) int get_next_source_symbol_id(picoquic_cnx_t *cnx, framework_sender_t sender, source_symbol_id_t *ret) {
    return (int) run_noparam(cnx, FEC_PROTOOP_GET_NEXT_SOURCE_SYMBOL_ID, 1, &sender, &ret);
}

static __attribute__((always_inline)) int reserve_src_fpi_frame(picoquic_cnx_t *cnx, source_symbol_id_t id) {
    reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
    if (!slot)
        return PICOQUIC_ERROR_MEMORY;

    my_memset(slot, 0, sizeof(reserve_frame_slot_t));

    slot->frame_type = FRAME_FEC_SRC_FPI;
    slot->nb_bytes = 1 + MAX_SRC_FPI_SIZE;
    slot->is_congestion_controlled = false;
    slot->low_priority = true;
    slot->frame_ctx = id;
    if (reserve_frames(nx, 1, slot) != 1 + MAX_SRC_FPI_SIZE)
        return PICOQUIC_ERROR_MEMORY;
    return 0;
}





#endif //FEC_UTILS_H
