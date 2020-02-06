#include "../fec.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"
#include "tetrys_framework.h"
#include "wire.h"

#define INITIAL_SYMBOL_ID 1
#define MAX_QUEUED_REPAIR_SYMBOLS 6
#define NUMBER_OF_SYMBOLS_TO_FLUSH 5

typedef uint32_t fec_block_number;

static __attribute__((always_inline)) void tetrys_id_has_landed(tetrys_fec_framework_sender_t *wff,
                                                                tetrys_source_symbol_id_t id) {
    // if reordering is present, we might reduce this value instead of increasing it but at least we handle the case where the window wraps around
    wff->last_landed_id = id;
}

static __attribute__((always_inline)) void tetrys_id_takes_off(tetrys_fec_framework_sender_t *wff,
                                                               tetrys_source_symbol_id_t id) {
    wff->last_sent_id = id;

}

static __attribute__((always_inline)) void tetrys_packet_has_been_recovered(picoquic_cnx_t *cnx, plugin_state_t *state, tetrys_fec_framework_sender_t *wff, uint64_t pn, tetrys_source_symbol_id_t first_id) {
//    enqueue_recovered_packet_to_buffer(wff->rps, pn);
    enqueue_recovered_packet_to_buffer(&state->recovered_packets, pn);
}

static __attribute__((always_inline)) void tetrys_process_recovered_packets(picoquic_cnx_t *cnx, plugin_state_t *state, tetrys_fec_framework_sender_t *wff, const uint8_t *size_and_packets) {
    uint64_t n_packets = *((uint64_t *) size_and_packets);
    uint64_t *packet_numbers = (uint64_t *) (size_and_packets + sizeof(uint64_t));
    tetrys_source_symbol_id_t *ids = (tetrys_source_symbol_id_t  *) (packet_numbers + n_packets);
    for (int i = 0 ; i < n_packets ; i++) {
        tetrys_packet_has_been_recovered(cnx, state, wff, packet_numbers[i], ids[i]);
    }
}

static __attribute__((always_inline)) tetrys_fec_framework_sender_t *tetrys_create_framework_sender(picoquic_cnx_t *cnx) {
    tetrys_fec_framework_sender_t *ff = my_malloc(cnx, sizeof(tetrys_fec_framework_sender_t));
    if (!ff) return NULL;
    if (tetrys_init_framework(cnx, &ff->common_fec_framework) != 0) {
        my_free(cnx, ff);
        return NULL;
    }
    ff->source_symbol_added_since_flush = false;
    ff->last_landed_id = 0;
    ff->last_sent_id = 0;
    return ff;
}

/**
 * The repair frame format for tetrys will be this one :
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  N Repair Symbols (16 bits) |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Size of symbol n (16 nits) |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+     --> N times
 * |                Repair Symbol n Payload                |
 * |                                                       |
 * |                            ...                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static __attribute__((always_inline)) int tetrys_reserve_repair_frames(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ff, size_t size_max, uint16_t symbol_size, bool feedback_implied) {
    if (size_max < TETRYS_REPAIR_FRAME_HEADER_SIZE + symbol_size) {
        PROTOOP_PRINTF(cnx, "NOT ENOUGH SPACE TO RESERVE A REPAIR FRAME\n");
        return -1;
    }
    tetrys_fec_framework_t *wff = (tetrys_fec_framework_t *) ff;
    update_tetrys_state(cnx, wff);
    while (buffer_peek_symbol_payload_size(cnx, &wff->buffered_repair_symbols, MIN(SERIALIZATION_BUFFER_SIZE, size_max)) > 0) {
        uint16_t n_repair_symbols = 0;
        uint16_t total_frame_bytes = 1 + sizeof(uint16_t); // type byte + n_repair_symbols field
        int size;
        tetrys_repair_frame_t *repair_frame = create_tetrys_repair_frame_without_symbols(cnx);
        if (!repair_frame)
            return PICOQUIC_ERROR_MEMORY;
        repair_frame->symbols = my_malloc(cnx, (size_max/symbol_size + 1)*sizeof(tetrys_repair_symbol_t *));
        while ((size = buffer_dequeue_symbol_payload_skip_old_ones(cnx, ff, &wff->buffered_repair_symbols, wff->buffer,
                                                                   MIN(SERIALIZATION_BUFFER_SIZE,
                                                                       size_max - total_frame_bytes))) > 0) {
            // TODO: see if it makes sense to encode the first ID in the repair frame
            tetrys_repair_symbol_t *rs = create_tetrys_repair_symbol(cnx, size);
            if (!rs) {
                return PICOQUIC_ERROR_MEMORY;
            }
//        my_memcpy(bytes+sizeof(uint16_t), wff->buffer + sizeof(source_fpid_t) + 4, repair_frame->header.data_length - sizeof(uint16_t));
            my_memcpy(rs->repair_symbol.repair_payload, wff->buffer, size);
            repair_frame->symbols[n_repair_symbols++] = (repair_symbol_t *) rs;
            total_frame_bytes += sizeof(uint16_t) + size;   // we add the size_of_symbol_n field and the symbol n itself
        }
        repair_frame->n_repair_symbols = n_repair_symbols;
        reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
        if (!slot)
            return PICOQUIC_ERROR_MEMORY;
        my_memset(slot, 0, sizeof(reserve_frame_slot_t));
        slot->frame_type = FRAME_REPAIR;
        slot->nb_bytes = total_frame_bytes;
        slot->frame_ctx = repair_frame;
        slot->is_congestion_controlled = !feedback_implied;
        size_t reserved_size = reserve_frames(cnx, 1, slot);
        if (reserved_size < slot->nb_bytes) {
            PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
            delete_tetrys_repair_frame(cnx, repair_frame);
            my_free(cnx, slot);
            return 1;
        }
    }
    return 0;
}


static __attribute__((always_inline)) int tetrys_protect_source_symbol(picoquic_cnx_t *cnx, plugin_state_t *state,
                                                                       tetrys_fec_framework_sender_t *ffs,
                                                                       tetrys_source_symbol_t *ss, uint16_t symbol_size) {
    if (symbol_size + 1 > SERIALIZATION_BUFFER_SIZE) {
        PROTOOP_PRINTF(cnx, "ERROR SYMBOL SIZE, %d > %d\n", symbol_size + 1, SERIALIZATION_BUFFER_SIZE);
        return -1;
    }
    tetrys_fec_framework_t *ff = &ffs->common_fec_framework;
    ff->buffer[0] = TYPE_PAYLOAD_TO_PROTECT;
    my_memcpy(ff->buffer+1, ss->source_symbol._whole_data, symbol_size);
    if (send(ff->unix_sock_fd, ff->buffer, 1 + symbol_size, 0) != 1 + symbol_size) {
        PROTOOP_PRINTF(cnx, "ERROR SEND, ERRNO = %d\\n\", get_errno()\n");
        return -1;
    }
    int size;
    while ((size = recv(ff->unix_sock_fd, ff->buffer, SERIALIZATION_BUFFER_SIZE, 0)) >= 0 && ff->buffer[0] != TYPE_SOURCE_SYMBOL) {
        tetrys_handle_message(cnx, state, ff, ff->buffer, size);
    }
    if (size < 0) {
        PROTOOP_PRINTF(cnx, "ERROR RECV, ERRNO = %d\n", get_errno());
        return -1;
    }
    // here, we know that the type is TYPE_SOURCE_SYMBOL
    ss->id = decode_u32(&ff->buffer[1]);
    uint16_t recv_symbol_size = size - 1 - sizeof(tetrys_source_symbol_id_t);
    if (recv_symbol_size != symbol_size) {
        PROTOOP_PRINTF(cnx, "ERROR SIZE\n");
        return -1;
    }
    ffs->source_symbol_added_since_flush = true;
    return 0;
}


static __attribute__((always_inline)) int tetrys_protect_packet_payload(picoquic_cnx_t *cnx, plugin_state_t *state, tetrys_fec_framework_sender_t *ff,
                                                                        uint8_t *payload, size_t payload_length, uint64_t packet_number,
                                                                        source_symbol_id_t *first_symbol_id, uint16_t *n_chunks, size_t symbol_size) {
    *n_chunks = 0;
    source_symbol_t **sss = packet_payload_to_source_symbols(cnx, payload, payload_length, symbol_size, packet_number, n_chunks, sizeof(tetrys_source_symbol_t));
    if (!sss)
        return PICOQUIC_ERROR_MEMORY;
    for (int i = 0 ; i < *n_chunks ; i++) {
        int err = tetrys_protect_source_symbol(cnx, state, ff, (tetrys_source_symbol_t *) sss[i], symbol_size);
        if (err) {
            my_free(cnx, sss);
            return err;
        }
        if (i == 0)
            *first_symbol_id = ((tetrys_source_symbol_t *) sss[i])->id;
    }
    my_free(cnx, sss);
    return 0;
}


static __attribute__((always_inline)) int flush_tetrys(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ffs, uint16_t symbol_size) {
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
    return 0;
}