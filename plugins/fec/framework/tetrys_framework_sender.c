#include "../fec.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"
#include "../fec_protoops.h"
#include "tetrys_framework.h"

#define INITIAL_SYMBOL_ID 1
#define MAX_QUEUED_REPAIR_SYMBOLS 6
#define NUMBER_OF_SYMBOLS_TO_FLUSH 5

typedef uint32_t fec_block_number;

static __attribute__((always_inline)) void sfpid_has_landed(tetrys_fec_framework_sender_t *wff, source_fpid_t sfpid) {
    // if reordering is present, we might reduce this value instead of increasing it but at least we handle the case where the window wraps around
    wff->last_landed_id = sfpid;
}

static __attribute__((always_inline)) void sfpid_takes_off(tetrys_fec_framework_sender_t *wff, source_fpid_t sfpid) {
    wff->last_sent_id = sfpid;

}

static __attribute__((always_inline)) tetrys_fec_framework_sender_t *tetrys_create_framework_sender(picoquic_cnx_t *cnx) {
    tetrys_fec_framework_sender_t *ff = my_malloc(cnx, sizeof(tetrys_fec_framework_sender_t));
    if (!ff) return NULL;
    if (tetrys_init_framework(cnx, &ff->common_fec_framework) != 0) {
        my_free(cnx, ff);
        return NULL;
    }
    ff->source_symbol_added_since_flush = false;
    ff->last_landed_id.raw = 0;
    ff->last_sent_id.raw = 0;
    return ff;
}

static __attribute__((always_inline)) int reserve_fec_frames(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ff, size_t size_max) {
    if (size_max <= sizeof(fec_frame_header_t))
        return -1;
    int size;
    tetrys_fec_framework_t *wff = (tetrys_fec_framework_t *) ff;
    update_tetrys_state(cnx, wff);
    while ((size = buffer_dequeue_symbol_payload_skip_old_ones(cnx, ff, &wff->buffered_repair_symbols, wff->buffer, SERIALIZATION_BUFFER_SIZE)) > 0) {
        fec_frame_t *fecframe = my_malloc(cnx, sizeof(fec_frame_t));
        if (!fecframe)
            return PICOQUIC_ERROR_MEMORY;
        my_memset(fecframe, 0, sizeof(fec_frame_t));
        uint8_t *bytes = my_malloc(cnx, (unsigned int) (size_max - (1 + sizeof(fec_frame_header_t))));
        if (!bytes)
            return PICOQUIC_ERROR_MEMORY;
        fecframe->header.repair_fec_payload_id.source_fpid.raw = decode_u32(wff->buffer);
        fecframe->header.data_length = size - sizeof(source_fpid_t) - sizeof(uint16_t);  // we remove the 6 bytes of metadata in the symbol: they are present in the frame header, except the payload length without the block
        // copy the data length
        my_memcpy(bytes, wff->buffer + sizeof(source_fpid_t), sizeof(uint16_t));
        my_memcpy(bytes+sizeof(uint16_t), wff->buffer + sizeof(source_fpid_t) + 4, fecframe->header.data_length - sizeof(uint16_t));
        fecframe->data = bytes;
        reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
        if (!slot)
            return PICOQUIC_ERROR_MEMORY;
        my_memset(slot, 0, sizeof(reserve_frame_slot_t));
        slot->frame_type = FEC_TYPE;
        slot->nb_bytes = 1 + sizeof(fec_frame_header_t) + fecframe->header.data_length;
        slot->frame_ctx = fecframe;
        slot->is_congestion_controlled = true;
        PROTOOP_PRINTF(cnx, "RESERVE FEC FRAMES, SIZE = %ld\n", size);
        size_t reserved_size = reserve_frames(cnx, 1, slot);
        if (reserved_size < slot->nb_bytes) {
            PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
            my_free(cnx, fecframe->data);
            my_free(cnx, fecframe);
            my_free(cnx, slot);
            return 1;
        }
    }
    return 0;
}


static __attribute__((always_inline)) source_fpid_t get_source_fpid(tetrys_fec_framework_t *wff) {
    return (source_fpid_t) 0u;
}

static __attribute__((always_inline)) int protect_source_symbol(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ffs, source_symbol_t *ss) {
    if (ss->data_length + 1 > SERIALIZATION_BUFFER_SIZE) {
        PROTOOP_PRINTF(cnx, "ERROR DATA LENGTH, %d > %d\n", ss->data_length + 1, SERIALIZATION_BUFFER_SIZE);
        return -1;
    }
    tetrys_fec_framework_t *ff = &ffs->common_fec_framework;
    ff->buffer[0] = TYPE_PAYLOAD_TO_PROTECT;
    my_memcpy(ff->buffer+1, ss->data, ss->data_length);
    if (send(ff->unix_sock_fd, ff->buffer, 1+ss->data_length, 0) != 1+ss->data_length) {
        PROTOOP_PRINTF(cnx, "ERROR SEND, ERRNO = %d\\n\", get_errno()\n");
        return -1;
    }
    int size;
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return -1;
    while ((size = recv(ff->unix_sock_fd, ff->buffer, SERIALIZATION_BUFFER_SIZE, 0)) >= 0 && ff->buffer[0] != TYPE_SOURCE_SYMBOL) {
        tetrys_handle_message(cnx, state, ff, ff->buffer, size);
    }
    if (size < 0) {
        PROTOOP_PRINTF(cnx, "ERROR RECV, ERRNO = %d\n", get_errno());
        return -1;
    }
    // here, we know that the type is TYPE_SOURCE_SYMBOL
    PROTOOP_PRINTF(cnx, "DONE\n");
    ss->source_fec_payload_id.raw = decode_u32(&ff->buffer[1]);
    uint16_t symbol_size = size - 1 - sizeof(source_fpid_t);
    if (symbol_size != ss->data_length) {
        PROTOOP_PRINTF(cnx, "ERROR SIZE\n");
        return -1;
    }
    ffs->source_symbol_added_since_flush = true;
    return 0;
}

static __attribute__((always_inline)) int flush_tetrys(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ffs) {
    tetrys_fec_framework_t *ff = &ffs->common_fec_framework;
    ff->buffer[0] = TYPE_MESSAGE;
    encode_u32(TETRYS_MESSAGE_GENERATE_REPAIR_PACKET, &ff->buffer[1]);
    if (ffs->source_symbol_added_since_flush) {
        // we flush, so remove the previously queued symbols and flush the new ones
        buffer_remove_old_symbol_payload(cnx, ffs, &ffs->common_fec_framework.buffered_repair_symbols, ffs->last_sent_id);
        for (int i = 0 ; i < NUMBER_OF_SYMBOLS_TO_FLUSH ; i++) {
            if (send(ff->unix_sock_fd, ff->buffer, 1+sizeof(tetrys_message_t), 0) != 1+sizeof(tetrys_message_t)) {
                PROTOOP_PRINTF(cnx, "ERROR SEND MESSAGE, ERRNO = %d\\n\", get_errno()\n");
                return -1;
            }
        }
    }

    ffs->source_symbol_added_since_flush = false;
    update_tetrys_state(cnx, ff);
    reserve_fec_frames(cnx, ffs, PICOQUIC_MAX_PACKET_SIZE);
    return 0;
}