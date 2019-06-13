#ifndef PICOQUIC_TETRYS_FRAMEWORK_H
#define PICOQUIC_TETRYS_FRAMEWORK_H

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../bpf.h"
#include "picoquic.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"

#define UNIX_SOCKET_SERVER_PATH "/tmp/tetrys_server_sock"
#define MAX_BUFFERED_SYMBOLS 50
#define SERIALIZATION_BUFFER_SIZE 2000
#define TYPE_SOURCE_SYMBOL 0
#define TYPE_REPAIR_SYMBOL 1
#define TYPE_RECOVERED_SOURCE_SYMBOL 2
#define TYPE_PAYLOAD_TO_PROTECT 3
#define TYPE_MESSAGE 4
#define TYPE_NEW_CONNECTION 0xFF

typedef uint32_t tetrys_message_t;
#define TETRYS_MESSAGE_SET_CODE_RATE                0x0     // encoder side
#define TETRYS_MESSAGE_SET_ACKNOWLEDGE_FREQUENCY    0x1     // decoder side
#define TETRYS_MESSAGE_SET_ACKNOWLEDGE_INTERVAL     0x2     // encoder side
#define TETRYS_MESSAGE_GENERATE_REPAIR_PACKET       0x4     // encoder side


typedef struct {
    uint8_t *symbol;
    int size;
} tetrys_symbol_slot_t;

typedef struct {
    tetrys_symbol_slot_t slots[MAX_BUFFERED_SYMBOLS];
    int start;
    int size;
} tetrys_symbol_buffer_t;

static __attribute__((always_inline)) bool buffer_enqueue_symbol_payload(picoquic_cnx_t *cnx, tetrys_symbol_buffer_t *buffer, uint8_t *payload, int size) {
    if (buffer->size == MAX_BUFFERED_SYMBOLS || size > SERIALIZATION_BUFFER_SIZE) return false;
    int idx = ((uint32_t) (buffer->start + buffer->size)) % MAX_BUFFERED_SYMBOLS;
    if (!buffer->slots[idx].symbol) buffer->slots[idx].symbol = my_malloc(cnx, SERIALIZATION_BUFFER_SIZE);
    if (!buffer->slots[idx].symbol) return false;
    my_memcpy(buffer->slots[idx].symbol, payload, size);
    buffer->slots[idx].size = size;
    buffer->size++;
    return true;
}

static __attribute__((always_inline)) int buffer_dequeue_symbol_payload(picoquic_cnx_t *cnx, tetrys_symbol_buffer_t *buffer, uint8_t *dest, int dest_size) {
    if (buffer->size == 0) return -1;
    int idx = buffer->start;
    if (buffer->slots[idx].size > dest_size) return -1;
    my_memcpy(dest, buffer->slots[idx].symbol, buffer->slots[idx].size);
    buffer->start = (uint32_t) (buffer->start + 1) % MAX_BUFFERED_SYMBOLS;
    buffer->size--;
    return buffer->slots[idx].size;
}

typedef struct {
    int unix_sock_fd;
    uint8_t *buffer;
    struct sockaddr_un peer_addr;
    struct sockaddr_un local_addr;
    tetrys_symbol_buffer_t buffered_repair_symbols;
    tetrys_symbol_buffer_t buffered_recovered_symbols;
} tetrys_fec_framework_t;


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

static __attribute__((always_inline)) int tetrys_handle_message(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff, uint8_t *message, ssize_t size) {
    tetrys_symbol_buffer_t *buf = NULL;
    switch (message[0]) {
        case TYPE_REPAIR_SYMBOL:
            buf = &ff->buffered_repair_symbols;
            break;
        case TYPE_RECOVERED_SOURCE_SYMBOL:
            buf = &ff->buffered_recovered_symbols;
            break;
    }
    if (!buf) return -1;
    buffer_enqueue_symbol_payload(cnx, buf, message + 1, size-1);    // enqueue the symbol, without the type byte
    if (size == -1) {
        int err = get_errno();
        if (err != EAGAIN && err != EWOULDBLOCK) return -1;
    }
    return 0;
}


static __attribute__((always_inline)) int update_tetrys_state(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff) {
    int size;
    while ((size = recv(ff->unix_sock_fd, ff->buffer, SERIALIZATION_BUFFER_SIZE, MSG_DONTWAIT)) > 0) {
        int err = tetrys_handle_message(cnx, ff, ff->buffer, size);
        if (err) return err;
    }
    if (size == -1) {
        int err = get_errno();
        if (err != EAGAIN && err != EWOULDBLOCK) return -1;
    }
    return 0;
}

#endif //PICOQUIC_TETRYS_FRAMEWORK_H
