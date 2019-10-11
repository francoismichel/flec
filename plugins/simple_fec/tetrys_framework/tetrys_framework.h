#ifndef PICOQUIC_TETRYS_FRAMEWORK_H
#define PICOQUIC_TETRYS_FRAMEWORK_H

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../fec.h"
#include "picoquic.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"
#include "search_structures.h"

#define TETRYS_ACK_FRAME_TYPE 0x3c

#define UNIX_SOCKET_SERVER_PATH "/tmp/tetrys_server_sock"
#define MAX_BUFFERED_SYMBOLS 50
#define MAX_RECOVERED_SYMBOLS 150
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
    source_symbol_id_t highest_sent_id_when_enqueued;
} tetrys_symbol_slot_t;

typedef struct {
    tetrys_symbol_slot_t slots[MAX_BUFFERED_SYMBOLS];
    int start;
    int size;
} tetrys_symbol_buffer_t;

typedef uint32_t tetrys_source_symbol_id_t;
typedef struct {
    source_symbol_t source_symbol;
    tetrys_source_symbol_id_t id;
} tetrys_source_symbol_t;

typedef struct {
    int unix_sock_fd;
    uint8_t *buffer;
    struct sockaddr_un peer_addr;
    struct sockaddr_un local_addr;
    tetrys_source_symbol_t **recovered_source_symbols;
    tetrys_symbol_buffer_t buffered_repair_symbols;
    min_max_pq_t recovered_packets;
} tetrys_fec_framework_t;

typedef struct {
    tetrys_fec_framework_t common_fec_framework;
    source_symbol_id_t last_sent_id;
    source_symbol_id_t last_landed_id;
    bool source_symbol_added_since_flush;
    uint8_t *address_of_written_fpi_frame_payload;
} tetrys_fec_framework_sender_t;


typedef struct tetrys_repair_symbols_metadata {
    tetrys_source_symbol_id_t first_id;
    uint16_t n_protected_symbols;
} tetrys_repair_symbols_metadata_t;

typedef struct {
    repair_symbol_t repair_symbol;
} tetrys_repair_symbol_t;


typedef struct __attribute__((__packed__)) {
    uint16_t length;
} tetrys_ack_frame_header_t;

typedef struct __attribute__((__packed__)) {
    tetrys_ack_frame_header_t header;
    uint8_t *data;
} tetrys_ack_frame_t;



static __attribute__((always_inline)) tetrys_source_symbol_t *create_tetrys_source_symbol(picoquic_cnx_t *cnx, uint16_t symbol_size) {
    tetrys_source_symbol_t *ss = my_malloc(cnx, sizeof(tetrys_source_symbol_t));
    if (!ss)
        return NULL;
    my_memset(ss, 0, sizeof(tetrys_source_symbol_t));

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

static __attribute__((always_inline)) void delete_tetrys_source_symbol(picoquic_cnx_t *cnx, tetrys_source_symbol_t *ss) {
    delete_source_symbol(cnx, (source_symbol_t *) ss);
}

static __attribute__((always_inline)) tetrys_repair_symbol_t *create_tetrys_repair_symbol(picoquic_cnx_t *cnx, uint16_t symbol_size) {
    tetrys_repair_symbol_t *rs = my_malloc(cnx, sizeof(tetrys_source_symbol_t));
    if (!rs)
        return NULL;
    my_memset(rs, 0, sizeof(tetrys_repair_symbol_t));

    rs->repair_symbol.repair_payload = my_malloc(cnx, symbol_size*sizeof(uint8_t));
    if (!rs->repair_symbol.repair_payload) {
        my_free(cnx, rs);
        return NULL;
    }
    my_memset(rs->repair_symbol.repair_payload, 0, symbol_size*sizeof(uint8_t));
    rs->repair_symbol.payload_length = symbol_size;
    return rs;
}

static __attribute__((always_inline)) void delete_tetrys_repair_symbol(picoquic_cnx_t *cnx, tetrys_repair_symbol_t *rs) {
    delete_repair_symbol(cnx, (repair_symbol_t *) rs);
}





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

static __attribute__((always_inline)) bool buffer_enqueue_symbol_payload(picoquic_cnx_t *cnx, plugin_state_t *state, tetrys_symbol_buffer_t *buffer, uint8_t *payload, int size) {
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

static __attribute__((always_inline)) int buffer_peek_symbol_payload_size(picoquic_cnx_t *cnx, tetrys_symbol_buffer_t *buffer, int max_size) {
    if (buffer->size == 0) return 0;
    int idx = buffer->start;
    int size = buffer->slots[idx].size;
    if (size > max_size) return 0;
    return size;
}

static __attribute__((always_inline)) bool sent_after(tetrys_fec_framework_sender_t *ff, source_symbol_id_t candidate, source_symbol_id_t cmp) {
    // FIXME: handle window wrap-around
    return candidate > cmp;
}

static __attribute__((always_inline)) void buffer_remove_old_symbol_payload(picoquic_cnx_t *cnx, tetrys_fec_framework_sender_t *ff, tetrys_symbol_buffer_t *buffer, source_symbol_id_t after) {
    while (buffer->size > 0) {
        int idx = buffer->start;
        if (after != 0 && !sent_after(ff, buffer->slots[idx].highest_sent_id_when_enqueued,
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
    PROTOOP_PRINTF(cnx, "INIT FRAMEWORK\n");

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
        ff->recovered_source_symbols = my_malloc(cnx, MAX_RECOVERED_SYMBOLS*sizeof(source_symbol_t *));
        if (!ff->recovered_source_symbols) {
            my_free(cnx, ff);
            PROTOOP_PRINTF(cnx, "RECOVERED BUFFER FAIL\n");
            return -1;
        }
        my_memset(ff->recovered_source_symbols, 0, MAX_RECOVERED_SYMBOLS*sizeof(source_symbol_t *));

        ff->recovered_packets = create_min_max_pq(cnx, MAX_BUFFERED_SYMBOLS);
        if (!ff->recovered_packets) {
            PROTOOP_PRINTF(cnx, "COULD NOT CREATE THE RECOVERED PACKETS PQUEUE\n");
            my_free(cnx, ff);
            my_free(cnx, ff->recovered_source_symbols);
            my_free(cnx, ff->buffer);
            return -1;
        }

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

static __attribute__((always_inline)) int tetrys_serialize_source_symbol(picoquic_cnx_t *cnx, tetrys_source_symbol_t *ss, uint8_t *buf, ssize_t max_size, ssize_t *written, uint16_t symbol_size) {
    *written = 0;
    if (max_size >= symbol_size + sizeof(uint32_t) + 4 + 1) {    // tetrys header = uint32 + length (uint16) + flags (uint16) + symbol type (uint8)
        buf[0] = TYPE_SOURCE_SYMBOL;
        *written += 1;
        encode_u32(ss->id, buf + *written);
        *written = *written + sizeof(uint32_t);
        encode_u16(symbol_size, buf + *written);
        *written = *written + 2;
        my_memset(buf + *written, 0, 2);
        *written = *written + 2;
        my_memcpy(buf + *written, ss->source_symbol._whole_data, symbol_size);
        *written = *written + symbol_size;
        return 0;
    }
    return -1;
}

static __attribute__((always_inline)) int tetrys_serialize_repair_symbol(picoquic_cnx_t *cnx, tetrys_repair_symbol_t *rs, uint8_t *buf, ssize_t max_size, ssize_t *written) {
    *written = 0;
    if (max_size >= rs->repair_symbol.payload_length + sizeof(uint32_t) + 4 + 1) {    // tetrys header = uint32 + length (uint16) + flags (uint16) + symbol type (uint8)
        buf[0] = TYPE_REPAIR_SYMBOL;
        *written += 1;
        // TODO: see if encoding the first id is really needed
//        encode_u32(0, buf + *written);
//        *written += sizeof(uint32_t);
        // copy the payload data length
//        my_memcpy(buf + *written, rs->repair_symbol.repair_payload, sizeof(uint16_t));
//        *written += sizeof(uint16_t);
//        buf[*written]   = 0;
//        buf[*written+1] = 1;    // indicate that this is a RS
//        *written += 2;
        my_memcpy(buf + *written, rs->repair_symbol.repair_payload/* + sizeof(uint16_t)*/, rs->repair_symbol.payload_length/*-sizeof(uint16_t)*/);
        *written += rs->repair_symbol.payload_length;//-sizeof(uint16_t);
        return 0;
    }
    return -1;
}

// returns true if it succeeded, false otherwise
static __attribute__((always_inline)) bool tetrys_reassemble_packet_from_recovered_symbol(picoquic_cnx_t *cnx, tetrys_fec_framework_t *wff, uint8_t *buffer,
                                                                                          size_t buffer_size, tetrys_source_symbol_t **source_symbols, uint16_t n_source_symbols, uint32_t recovered_symbol_idx,
                                                                                          size_t *payload_size, uint64_t *packet_number, tetrys_source_symbol_id_t *first_id_in_packet) {
    // first, find the start of the packet backwards
    int start_index = -1;
    PROTOOP_PRINTF(cnx, "TRY TO REASSEMBLE\n");
    for (int current_index = recovered_symbol_idx ; current_index >= 0; current_index--) {
        PROTOOP_PRINTF(cnx, "CONSIDER SYMBOL AT INDEX %d\n", current_index);
        if (!source_symbols[current_index]) {
            // the packet cannot be complete, so we failed reassembling it
            return false;
        }
        if (get_ss_metadata_S(&source_symbols[current_index]->source_symbol)) {
            start_index = current_index;
            if (get_ss_metadata_N(&source_symbols[current_index]->source_symbol)) {
                // let's get the packet number
                *packet_number = decode_u64(source_symbols[current_index]->source_symbol.chunk_data);
            }
            break;
        }
    }
    if (start_index == -1)
        return false;
    // now, find the end of the packet
    int end_index = -1;
    for (int current_index = recovered_symbol_idx ; current_index < n_source_symbols; current_index++) {
        if (!source_symbols[current_index]) {
            // the packet cannot be complete, so we failed reassembling it
            return false;
        }
        if (get_ss_metadata_E(&source_symbols[current_index]->source_symbol)) {
            end_index = current_index;
            break;
        }
    }
    if (end_index == -1)
        return false;

    *payload_size = 0;
    // we found the start and the end, let's reassemble the packet
    for (int i = start_index ; i <= end_index ; i++) {
        // TODO: uncomment this when the malloc limit is removed
//        if (*payload_size + source_symbols[i]->source_symbol.chunk_size > buffer_size) {
//            PROTOOP_PRINTF(cnx, "PACKET IS TOO BIG TO BE REASSEMBLED !! %u > %u\n", *payload_size + source_symbols[i]->source_symbol.chunk_size, buffer_size);
//            // packet too big, should never happen
//            return false;
//        }

        // we skip the packet number if it is present in the symbol
        uint8_t data_offset = get_ss_metadata_N(&source_symbols[i]->source_symbol) ? sizeof(uint64_t) : 0;
        // copy the chunk into the packet
        my_memcpy(&buffer[*payload_size], source_symbols[i]->source_symbol.chunk_data + data_offset, source_symbols[i]->source_symbol.chunk_size - data_offset);
        *payload_size += source_symbols[i]->source_symbol.chunk_size - data_offset;
    }

    PROTOOP_PRINTF(cnx, "SUCCESSFULLY REASSEMBLED !!\n");

    // the packet has been reassembled !

    *first_id_in_packet = source_symbols[start_index]->id;

    return true;
}

static __attribute__((always_inline)) int tetrys_handle_message(picoquic_cnx_t *cnx, plugin_state_t *state, tetrys_fec_framework_t *ff, uint8_t *message, ssize_t size) {
    tetrys_symbol_buffer_t *buf = NULL;
    PROTOOP_PRINTF(cnx, "HANDLE MESSAGE TYPE 0x%x\n", message[0]);
    switch (message[0]) {
        case TYPE_REPAIR_SYMBOL:
            buf = &ff->buffered_repair_symbols;
            break;
        case TYPE_RECOVERED_SOURCE_SYMBOL:;
        PROTOOP_PRINTF(cnx, "RECEIVED RECOVERED SYMBOL\n");
            if (state->symbol_size != size - 1 - sizeof(tetrys_source_symbol_id_t)) {
                PROTOOP_PRINTF(cnx, "WRONG SOURCE SYMBOL SIZE\n");
                return -1;
            }

            tetrys_source_symbol_t *ss = create_tetrys_source_symbol(cnx, state->symbol_size);
            if (!ss) {
                PROTOOP_PRINTF(cnx, "COULD NOT CREATE A SOURCE SYMBOL\n");
                return PICOQUIC_ERROR_MEMORY;
            }
            ss->id = decode_u32(message + 1);
            my_memcpy(ss->source_symbol._whole_data, message + 1 + sizeof(tetrys_source_symbol_id_t), state->symbol_size);
            ff->recovered_source_symbols[ss->id % MAX_RECOVERED_SYMBOLS] = ss;
            size_t payload_size = 0;
            uint64_t packet_number = 0;
            tetrys_source_symbol_id_t first_id_in_packet = 0;
            if (tetrys_reassemble_packet_from_recovered_symbol(cnx, ff, ff->buffer, SERIALIZATION_BUFFER_SIZE, ff->recovered_source_symbols, MAX_RECOVERED_SYMBOLS, ss->id % MAX_RECOVERED_SYMBOLS,
                    &payload_size, &packet_number, &first_id_in_packet)) {

                int err = picoquic_decode_frames_without_current_time(cnx, ff->buffer, payload_size, 3, (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0));
                if (err) {
                    PROTOOP_PRINTF(cnx, "ERROR WHILE DECODING RECOVERED PACKET: %d\n", err);
                } else {
                    PROTOOP_PRINTF(cnx, "RECOVERED PACKET %lx SUCCESSFULLY PARSED, QUEUE SIZE BEFORE = %d\n", packet_number, ff->recovered_packets->size);
                    // record this packet recovery
                    pq_insert(ff->recovered_packets, packet_number, (void *) (uint64_t) first_id_in_packet);
                    PROTOOP_PRINTF(cnx, "QUEUE SIZE AFTER = %d\n", packet_number, ff->recovered_packets->size);
                }
            }
            return 0;
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
    PROTOOP_PRINTF(cnx, "TETRYS UPDATE STATE\n");
    plugin_state_t *state = get_plugin_state(cnx);
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
