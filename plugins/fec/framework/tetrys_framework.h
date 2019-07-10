#ifndef PICOQUIC_TETRYS_FRAMEWORK_H
#define PICOQUIC_TETRYS_FRAMEWORK_H

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../fec_protoops.h"
#include "picoquic.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"

#define TETRYS_ACK_FRAME_TYPE 0x3c

#define UNIX_SOCKET_SERVER_PATH "/tmp/tetrys_server_sock"
#define MAX_BUFFERED_SYMBOLS 50
#define SERIALIZATION_BUFFER_SIZE 2000
#define TYPE_SOURCE_SYMBOL 0
#define TYPE_REPAIR_SYMBOL 1
#define TYPE_RECOVERED_SOURCE_SYMBOL 2
#define TYPE_PAYLOAD_TO_PROTECT 3
#define TYPE_MESSAGE 4
#define TYPE_ACK 5
#define TYPE_NEW_CONNECTION 0xFF

typedef uint32_t tetrys_message_t;
#define TETRYS_MESSAGE_SET_CODE_RATE                0x0     // encoder side
#define TETRYS_MESSAGE_SET_ACKNOWLEDGE_FREQUENCY    0x1     // decoder side
#define TETRYS_MESSAGE_SET_ACKNOWLEDGE_INTERVAL     0x2     // encoder side
#define TETRYS_MESSAGE_GENERATE_REPAIR_PACKET       0x4     // encoder side

typedef struct {
    uint8_t *symbol;
    int size;
    source_fpid_t highest_sent_id_when_enqueued;
} tetrys_symbol_slot_t;

typedef struct {
    tetrys_symbol_slot_t slots[MAX_BUFFERED_SYMBOLS];
    int start;
    int size;
} tetrys_symbol_buffer_t;

typedef struct {
    int unix_sock_fd;
    uint8_t *buffer;
    struct sockaddr_un peer_addr;
    struct sockaddr_un local_addr;
    tetrys_symbol_buffer_t buffered_repair_symbols;
    tetrys_symbol_buffer_t buffered_recovered_symbols;
} tetrys_fec_framework_t;

typedef struct {
    tetrys_fec_framework_t common_fec_framework;
    source_fpid_t last_sent_id;
    source_fpid_t last_landed_id;
    bool source_symbol_added_since_flush;
} tetrys_fec_framework_sender_t;


typedef struct __attribute__((__packed__)) {
    uint16_t length;
} tetrys_ack_frame_header_t;

typedef struct __attribute__((__packed__)) {
    tetrys_ack_frame_header_t header;
    uint8_t *data;
} tetrys_ack_frame_t;

static __attribute__((always_inline)) int parse_tetrys_ack_frame_header(tetrys_ack_frame_header_t *header, uint8_t *bytes, const uint8_t *bytes_max) {
    if (bytes + sizeof(tetrys_ack_frame_header_t) > bytes_max) {
        return -1;
    }
    uint8_t bytes_header[2];
    my_memcpy(bytes_header, bytes, 2);
    header->length = decode_u16(bytes_header);
    return 0;
}
static __attribute__((always_inline)) size_t write_tetrys_ack_frame_header(picoquic_cnx_t *cnx, tetrys_ack_frame_header_t *header, uint8_t *buffer) {
    size_t consumed = 0;
    uint8_t type_byte = TETRYS_ACK_FRAME_TYPE;
    my_memcpy(buffer, &type_byte, sizeof(type_byte));
    consumed++;
    encode_u16(header->length, buffer + consumed);
    consumed += sizeof(tetrys_ack_frame_header_t);
    return consumed;
}


// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
//FIXME: we pass the state in the parameters because the call to get_bpf_state leads to an error when loading the code
static __attribute__((always_inline)) bool tetrys_receive_ack(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff, tetrys_ack_frame_t *ack_frame){
    ff->buffer[0] = TYPE_ACK;
    my_memcpy(ff->buffer+1, ack_frame->data, ack_frame->header.length);
    if (send(ff->unix_sock_fd, ff->buffer, 1 + ack_frame->header.length, 0) != 1 + ack_frame->header.length) {
        PROTOOP_PRINTF(cnx, "SERIALIZATION ERROR\n");
        return false;
    }
    return true;
}

static __attribute__((always_inline)) bool buffer_enqueue_symbol_payload(picoquic_cnx_t *cnx, bpf_state *state, tetrys_symbol_buffer_t *buffer, uint8_t *payload, int size) {
    if (buffer->size == MAX_BUFFERED_SYMBOLS || size > SERIALIZATION_BUFFER_SIZE) return false;
    int idx = ((uint32_t) (buffer->start + buffer->size)) % MAX_BUFFERED_SYMBOLS;
    if (!buffer->slots[idx].symbol) buffer->slots[idx].symbol = my_malloc(cnx, SERIALIZATION_BUFFER_SIZE);
    if (!buffer->slots[idx].symbol) return false;
    my_memcpy(buffer->slots[idx].symbol, payload, size);
    buffer->slots[idx].size = size;
    buffer->slots[idx].highest_sent_id_when_enqueued = ((tetrys_fec_framework_sender_t *)state->framework_sender)->last_sent_id;
    buffer->size++;
    return true;
}

static __attribute__((always_inline)) void buffer_dequeue_symbol_payload_without_copy(picoquic_cnx_t *cnx, tetrys_symbol_buffer_t *buffer) {
    if (buffer->size == 0) return;
    buffer->start = (uint32_t) (buffer->start + 1) % MAX_BUFFERED_SYMBOLS;
    buffer->size--;
}


static __attribute__((always_inline)) int buffer_dequeue_symbol_payload(picoquic_cnx_t *cnx, tetrys_symbol_buffer_t *buffer, uint8_t *dest, int dest_size) {
    if (buffer->size == 0) return -1;
    int idx = buffer->start;
    int size = buffer->slots[idx].size;
    if (size > dest_size) return -1;
    my_memcpy(dest, buffer->slots[idx].symbol, buffer->slots[idx].size);
    buffer_dequeue_symbol_payload_without_copy(cnx, buffer);
    return size;
}

static __attribute__((always_inline)) bool sent_after(tetrys_fec_framework_sender_t *ff, source_fpid_t candidate, source_fpid_t cmp) {
    // FIXME: handle window wrap-around
    return candidate.raw > cmp.raw;
}

static __attribute__((always_inline)) void buffer_remove_old_symbol_payload(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ff, tetrys_symbol_buffer_t *buffer, source_fpid_t after) {
    while (buffer->size > 0) {
        int idx = buffer->start;
        if (after.raw != 0 && !sent_after(ff, buffer->slots[idx].highest_sent_id_when_enqueued,
                        after)) {    // check if the symbols protected by this repair symbol are still in flight
            // discard the symbol without copying it
            buffer_dequeue_symbol_payload_without_copy(cnx, buffer);
        } else {
            return;
        }
    }
}

// decodes the next symbol payload that has been sent after last_id. The other symbols will be removed from the buffer
static __attribute__((always_inline)) int buffer_dequeue_symbol_payload_skip_old_ones(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ff, tetrys_symbol_buffer_t *buffer, uint8_t *dest, int dest_size) {
    buffer_remove_old_symbol_payload(cnx, ff, buffer, ff->last_landed_id);
    if (buffer->size == 0) return 0;
    return buffer_dequeue_symbol_payload(cnx, buffer, dest, dest_size);
}

static __attribute__((always_inline)) int tetrys_init_framework(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff) {
    if (ff) {
        my_memset(ff, 0, sizeof(tetrys_fec_framework_t));
        if((ff->unix_sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
            my_free(cnx, ff);
            PROTOOP_PRINTF(cnx, "SOCKET FAIL\n");
            return -1;
        }
        ff->buffer = my_malloc(cnx, SERIALIZATION_BUFFER_SIZE);
        if (!ff->buffer) {
            my_free(cnx, ff);
            PROTOOP_PRINTF(cnx, "BUFFER FAIL\n");
            return -1;
        }
        my_memset(ff->buffer, 0, SERIALIZATION_BUFFER_SIZE);
        ff->local_addr.sun_family = AF_UNIX;
        ff->local_addr.sun_path[0] = '\0'; // we want an abstract domain unix socket
        // the socket addr is depending on the CID so that we could use tetrys with several different connections
        picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
        if (!path) return -1;
        picoquic_connection_id_t *cnxid = (picoquic_connection_id_t *) get_path(path, AK_PATH_LOCAL_CID, 0);
        if (!cnxid) return -1;
        uint8_t *cnxid_buf = (uint8_t *) get_cnxid(cnxid, AK_CNXID_ID);
        uint8_t cnxid_len = (uint8_t) get_cnxid(cnxid, AK_CNXID_LEN);
        ff->local_addr.sun_path[1] = cnxid_len;
        my_memcpy(&ff->local_addr.sun_path[2], cnxid_buf, cnxid_len);
        for (int i = 0 ; i < sizeof(ff->local_addr) ; i++) {
            PROTOOP_PRINTF(cnx, "%d -> %hhx\n", i, ff->local_addr.sun_path[i]);
        }
        if (bind(ff->unix_sock_fd,(struct sockaddr *)&ff->local_addr, sizeof(struct sockaddr_un)) < 0)  {
            my_free(cnx, ff);
            return -1;
        }
        ff->peer_addr.sun_family = AF_UNIX;
        ff->peer_addr.sun_path[0] = '\0';
        strncpy(&ff->peer_addr.sun_path[1], UNIX_SOCKET_SERVER_PATH, sizeof(ff->peer_addr));
        if (connect(ff->unix_sock_fd,(struct sockaddr *)&ff->peer_addr, sizeof(struct sockaddr_un)) < 0)  {
            my_free(cnx, ff);
            PROTOOP_PRINTF(cnx, "CONNECT FAILED: %d, size = %d\n", get_errno(), sizeof(struct sockaddr_un));
            return -1;
        }
        uint8_t type = TYPE_NEW_CONNECTION;
        send(ff->unix_sock_fd, &type, 1, 0);    // don't send the \0
        int size;
        if ((size = recv(ff->unix_sock_fd, ff->buffer, SERIALIZATION_BUFFER_SIZE, 0)) != strlen("connexion") || strncmp("connexion", (char *) ff->buffer, strlen("connexion")) != 0) {
            PROTOOP_PRINTF(cnx, "error when connecting, returned size %d\n", size);
        }
    }
    return 0;
}

static __attribute__((always_inline)) tetrys_fec_framework_t *tetrys_create_framework(picoquic_cnx_t *cnx) {
    tetrys_fec_framework_t *ff = my_malloc(cnx, sizeof(tetrys_fec_framework_t));
    if (!ff) return NULL;
    if (tetrys_init_framework(cnx, ff) != 0) {
        my_free(cnx, ff);
        return NULL;
    }
    return ff;
}

static __attribute__((always_inline)) int tetrys_serialize_source_symbol(picoquic_cnx_t *cnx, source_symbol_t *ss, uint8_t *buf, ssize_t max_size, ssize_t *written) {
    *written = 0;
    if (max_size >= ss->data_length + sizeof(uint32_t) + 4 + 1) {    // tetrys header = uint32 + length (uint16) + flags (uint16) + symbol type (uint8)
        buf[0] = TYPE_SOURCE_SYMBOL;
        *written += 1;
        encode_u32(ss->source_fec_payload_id.raw, buf + *written);
        *written = *written + sizeof(uint32_t);
        encode_u16(ss->data_length, buf + *written);
        *written = *written + 2;
        my_memset(buf + *written, 0, 2);
        *written = *written + 2;
        my_memcpy(buf + *written, ss->data, ss->data_length);
        *written = *written + ss->data_length;
        return 0;
    }
    return -1;
}

static __attribute__((always_inline)) int tetrys_serialize_repair_symbol(picoquic_cnx_t *cnx, repair_symbol_t *rs, uint8_t *buf, ssize_t max_size, ssize_t *written) {
    *written = 0;
    if (max_size >= rs->data_length + sizeof(uint32_t) + 4 + 1) {    // tetrys header = uint32 + length (uint16) + flags (uint16) + symbol type (uint8)
        buf[0] = TYPE_REPAIR_SYMBOL;
        *written += 1;
        encode_u32(rs->repair_fec_payload_id.source_fpid.raw, buf + *written);
        *written += sizeof(uint32_t);
        // copy the payload data length
        my_memcpy(buf + *written, rs->data, sizeof(uint16_t));
        *written += sizeof(uint16_t);
        buf[*written]   = 0;
        buf[*written+1] = 1;    // indicate that this is a RS
        *written += 2;
        my_memcpy(buf + *written, rs->data+sizeof(uint16_t), rs->data_length-sizeof(uint16_t));
        *written += rs->data_length-sizeof(uint16_t);
        return 0;
    }
    return -1;
}

static __attribute__((always_inline)) int tetrys_handle_message(picoquic_cnx_t *cnx, bpf_state *state, tetrys_fec_framework_t *ff, uint8_t *message, ssize_t size) {
    tetrys_symbol_buffer_t *buf = NULL;
    PROTOOP_PRINTF(cnx, "HANDLE MESSAGE TYPE 0x%x\n", message[0]);
    switch (message[0]) {
        case TYPE_REPAIR_SYMBOL:
            buf = &ff->buffered_repair_symbols;
            break;
        case TYPE_RECOVERED_SOURCE_SYMBOL:
            buf = &ff->buffered_recovered_symbols;
            break;
        case TYPE_ACK:;
            if (size > 0xFFFF)
                return -1;
            reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
            if (!slot)
                return PICOQUIC_ERROR_MEMORY;
            my_memset(slot, 0, sizeof(reserve_frame_slot_t));
            tetrys_ack_frame_t *frame = my_malloc(cnx, sizeof(tetrys_ack_frame_t));
            if (!frame) {
                my_free(cnx, slot);
                return PICOQUIC_ERROR_MEMORY;
            }
            my_memset(frame, 0, sizeof(tetrys_ack_frame_t));
            uint8_t *frame_data = my_malloc(cnx, size);
            if (!frame_data){
                my_free(cnx, slot);
                my_free(cnx, frame);
                return PICOQUIC_ERROR_MEMORY;
            }
            my_memset(frame_data, 0, size);
            frame->header.length = size;
            frame->data = frame_data;
            slot->frame_type = TETRYS_ACK_FRAME_TYPE;
            slot->nb_bytes = 1 + sizeof(tetrys_ack_frame_header_t) + size;
            slot->frame_ctx = frame;
            slot->is_congestion_controlled = true;
            reserve_frames(cnx, 1, slot);
            return 0;
    }
    if (!buf) return -1;
    buffer_enqueue_symbol_payload(cnx, state, buf, message + 1, size-1);    // enqueue the symbol, without the type byte
    if (size == -1) {
        int err = get_errno();
        if (err != EAGAIN && err != EWOULDBLOCK) return -1;
    }
    return 0;
}


static __attribute__((always_inline)) int update_tetrys_state(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff) {
    int size;
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return -1;
    while ((size = recv(ff->unix_sock_fd, ff->buffer, SERIALIZATION_BUFFER_SIZE, MSG_DONTWAIT)) > 0) {
        int err = tetrys_handle_message(cnx, state, ff, ff->buffer, size);
        if (err) return err;
    }
    if (size == -1) {
        int err = get_errno();
        if (err != EAGAIN && err != EWOULDBLOCK) return -1;
    }
    return 0;
}

#endif //PICOQUIC_TETRYS_FRAMEWORK_H
