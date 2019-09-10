
#ifndef FEC_UTILS_H
#define FEC_UTILS_H

#include <stdint.h>
#include <memcpy.h>

#define MIN(a, b) ((a < b) ? a : b)

#define member_size(struct_type, member) (sizeof(((struct_type *) 0)->member))

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
    return (int) run_noparam(cnx, FEC_PROTOOP_GET_NEXT_SOURCE_SYMBOL_ID, 1, &sender, (protoop_arg_t *) &ret);
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
    slot->frame_ctx = (void *) id;
    if (reserve_frames(cnx, 1, slot) != 1 + MAX_SRC_FPI_SIZE)
        return PICOQUIC_ERROR_MEMORY;
    return 0;
}


typedef struct lost_packet {
    uint64_t pn;
    uint64_t slot;
    uint32_t sfpid;
    struct lost_packet *next;
} lost_packet_t;

// we use a queue because it is more likely that we dequeue the packets in the order we inserted them
typedef struct lost_packet_queue {
    lost_packet_t *head;
    lost_packet_t *tail;
} lost_packet_queue_t;

// pre: queue != NULL
static __attribute__((always_inline)) int add_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue, uint64_t pn, uint64_t slot, uint32_t sfpid) {
    lost_packet_t *lp = my_malloc(cnx, sizeof(lost_packet_t));
    if (!lp) {
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(lp, 0, sizeof(lost_packet_t));
    lp->pn = pn;
    lp->slot = slot;
    lp->sfpid = sfpid;
    if (!queue->head && !queue->tail) {
        queue->head = queue->tail = lp;
        return 0;
    }
    queue->tail->next = lp;
    queue->tail = lp;
    return 0;
}

// sets *slot to the slot when the packet was sent, dequeues the packet from the queue and returns true if this packet was present
// if the packet was not present, does nothing and returns false
static __attribute__((always_inline)) bool dequeue_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue, uint64_t pn, uint64_t *slot, uint32_t *sfpid) {
    if (!queue->head && !queue->tail)
        return false;
    lost_packet_t *previous = NULL;
    lost_packet_t *current = queue->head;
    while (current) {
        if (current->pn == pn) {
            *slot = current->slot;
            *sfpid = current->sfpid;
            if (!previous) {
                // head == current
                queue->head = current->next;
            } else {
                previous->next = current->next;
            }
            if (queue->tail == current) {
                queue->tail = previous;
            }
            my_free(cnx, current);
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
}


// returns the packet number at first position of the queue (i.e. the oldest enqueued packet)
// returns -1 if the queue is empty
static __attribute__((always_inline)) int64_t get_first_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue) {
    if (!queue->head && !queue->tail)
        return -1;
    return queue->head->pn;
}

#define MAX_RECOVERED_PACKETS_IN_BUFFER 50
typedef struct {
    uint32_t start;
    uint32_t size;
    uint64_t packet_numbers[MAX_RECOVERED_PACKETS_IN_BUFFER];
} recovered_packets_buffer_t;


static __attribute__((always_inline)) recovered_packets_buffer_t *create_recovered_packets_buffer(picoquic_cnx_t *cnx) {
    recovered_packets_buffer_t *rpb = my_malloc(cnx, sizeof(recovered_packets_buffer_t));
    if (!rpb)
        return NULL;
    my_memset(rpb, 0, sizeof(recovered_packets_buffer_t));
    return rpb;
}

static __attribute__((always_inline)) void delete_recovered_packets_buffer(picoquic_cnx_t *cnx, recovered_packets_buffer_t *rpb) {
    my_free(cnx, rpb);
}

static __attribute__((always_inline)) void enqueue_recovered_packet_to_buffer(recovered_packets_buffer_t *b, uint64_t packet) {
    b->packet_numbers[(b->start + b->size) % MAX_RECOVERED_PACKETS_IN_BUFFER] = packet;
    if (b->size < MAX_RECOVERED_PACKETS_IN_BUFFER) b->size++;
    else {
        // we just removed the first enqueued packet, so shift the start
        b->start = (b->start + 1) % MAX_RECOVERED_PACKETS_IN_BUFFER;
    }
}

// pre: size > 0
static __attribute__((always_inline)) uint64_t peek_first_recovered_packet_in_buffer(recovered_packets_buffer_t *b) {
    return b->packet_numbers[b->start];
}

// pre: size > 0
static __attribute__((always_inline)) uint64_t dequeue_recovered_packet_from_buffer(recovered_packets_buffer_t *b) {
    if (b->size == 0) return -1;
    uint64_t packet = peek_first_recovered_packet_in_buffer(b);
    b->size--;
    b->start = (b->start + 1) % MAX_RECOVERED_PACKETS_IN_BUFFER;
    return packet;
}

static __attribute__((always_inline)) void enqueue_recovered_packets(recovered_packets_buffer_t *b, uint64_t *rp, uint64_t n_packets) {
    for(int i = 0 ; i < n_packets ; i++) {
        enqueue_recovered_packet_to_buffer(b, rp[i]);
    }
}




#endif //FEC_UTILS_H
