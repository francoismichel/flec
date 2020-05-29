
#ifndef FEC_UTILS_H
#define FEC_UTILS_H

#include <stdint.h>
#include <memcpy.h>
#include "../helpers.h"
#include "fec_constants.h"

#define member_size(struct_type, member) (sizeof(((struct_type *) 0)->member))

#define ALIGNMENT 32
static __attribute__((always_inline)) size_t align(size_t val) {
    return ( ( val - 1 ) | ( ((ALIGNMENT<<1) - 1 ) )) + 1;
}

static __attribute__((always_inline)) uint64_t decode_un(uint8_t *bytes, int n) {
    // we put 8 here instead of n because otherwise, we get an access to register 11 after compiling which is forbidden :'-)
    // but don't worry I love my job
    uint64_t retval = 0;
    uint8_t buffer[8];
    my_memcpy(buffer, bytes, n);
    int i;
    for (i = 0; i < n ; i++) {
        retval <<= 8;
        retval += buffer[i];
    }
    return retval;
}

static __attribute__((always_inline)) void encode_un(uint64_t to_encode, uint8_t *bytes, int n) {
    // same as for decode_un
    uint8_t buffer[8];
    int i;
    for (i = 0; i < n ; i++) {
        buffer[i] = (uint8_t) (to_encode >> 8*(n-i-1));
    }
    my_memcpy(bytes, buffer, n);
}

//static __attribute__((always_inline)) void encode_un(uint64_t to_encode, uint8_t *bytes, int n) {
//    uint8_t buffer[n];
//    int i;
//    for (i = 0; i < n ; i++) {
//        buffer[i] = (uint8_t) (to_encode >> 8*(n-i-1));
//    }
//    my_memcpy(bytes, buffer, n);
//}

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
    protoop_arg_t out = 0;
    int err = (int) run_noparam(cnx, FEC_PROTOOP_GET_NEXT_SOURCE_SYMBOL_ID, 1, &sender, &out);
    *ret = out;
    return err;
}


static __attribute__((always_inline)) int receive_packet_payload(picoquic_cnx_t *cnx, const uint8_t *payload, size_t payload_length,
        uint64_t packet_number, source_symbol_id_t first_symbol_id) {
    protoop_arg_t args[4];

    args[0] = (protoop_arg_t) payload;
    args[1] = (protoop_arg_t) payload_length;
    args[2] = (protoop_arg_t) packet_number;
    args[3] = (protoop_arg_t) first_symbol_id;


    int err = (int) run_noparam(cnx, FEC_RECEIVE_PACKET_PAYLOAD, 4, args, NULL);
    return err;
}


static __attribute__((always_inline)) int protect_packet_payload(picoquic_cnx_t *cnx, const uint8_t *payload, size_t payload_length,
        uint64_t packet_number, source_symbol_id_t *first_symbol_id, uint16_t *n_symbols) {
    protoop_arg_t args[3];

    args[0] = (protoop_arg_t) payload;
    args[1] = (protoop_arg_t) payload_length;
    args[2] = (protoop_arg_t) packet_number;

    protoop_arg_t out[2];

    int err = (int) run_noparam(cnx, FEC_PROTECT_PACKET_PAYLOAD, 3, args, out);

    *first_symbol_id = out[0];
    *n_symbols = out[1];
    return err;
}


static __attribute__((always_inline)) int reserve_repair_frames(picoquic_cnx_t *cnx, framework_sender_t sender, size_t size_max, size_t symbol_size,
                                        bool feedback_implied, bool protect_subset, source_symbol_id_t first_id_to_protect, uint16_t n_symbols_to_protect, protoop_arg_t *could_reserve) {
    protoop_arg_t args[7];

    args[0] = (protoop_arg_t) sender;
    args[1] = (protoop_arg_t) size_max;
    args[2] = (protoop_arg_t) symbol_size;
    args[3] = (protoop_arg_t) feedback_implied;
    args[4] = (protoop_arg_t) protect_subset;
    args[5] = (protoop_arg_t) first_id_to_protect;
    args[6] = (protoop_arg_t) n_symbols_to_protect;

    int err = (int) run_noparam(cnx, FEC_RESERVE_REPAIR_FRAMES, 7, args, could_reserve);
    return err;
}


static __attribute__((always_inline)) int fec_what_to_send(picoquic_cnx_t *cnx, picoquic_path_t *path, uint64_t current_time, available_slot_reason_t reason, what_to_send_t *wts, source_symbol_id_t *first_id_to_protect, uint16_t *n_symbols_to_protect) {
    protoop_arg_t args[3];
    args[0] = (protoop_arg_t) reason;
    args[1] = (protoop_arg_t) path;
    args[2] = (protoop_arg_t) current_time;
    protoop_arg_t out[3];

    int err = (what_to_send_t) run_noparam(cnx, FEC_PROTOOP_WHAT_TO_SEND, 3, args, out);
    *wts = out[0];
    *first_id_to_protect = out[1];
    *n_symbols_to_protect = out[2];
    return err;
}


static __attribute__((always_inline)) int fec_cancelled_packet(picoquic_cnx_t *cnx, what_to_send_t wts, source_symbol_id_t first_id, uint64_t n_symbols_to_protect) {
    protoop_arg_t args[3];
    args[0] = (protoop_arg_t) wts;
    args[1] = (protoop_arg_t) first_id;
    args[2] = (protoop_arg_t) n_symbols_to_protect;
    int err = (what_to_send_t) run_noparam(cnx, FEC_PROTOOP_CANCELLED_PACKET, 3, args, NULL);
    return err;
}


static __attribute__((always_inline)) what_to_send_t fec_available_slot(picoquic_cnx_t *cnx, picoquic_path_t *path, available_slot_reason_t reason, uint64_t current_time) {
    protoop_arg_t args[3];
    args[0] = (protoop_arg_t) path;
    args[1] = (protoop_arg_t) reason;
    args[2] = (protoop_arg_t) current_time;
    return (int) run_noparam(cnx, FEC_PROTOOP_AVAILABLE_SLOT, 3, args, NULL);
}


static __attribute__((always_inline)) what_to_send_t fec_check_for_available_slot(picoquic_cnx_t *cnx, available_slot_reason_t reason, uint64_t current_time) {
    protoop_arg_t args[2];
    args[0] = (protoop_arg_t) reason;
    args[1] = (protoop_arg_t) current_time;
    return (int) run_noparam(cnx, FEC_PROTOOP_CHECK_FOR_AVAILABLE_SLOT, 2, args, NULL);
}

// not specific to FEC
static __attribute__((always_inline)) int packet_has_been_acknowledged(picoquic_cnx_t *cnx,
                                                                              picoquic_path_t *path,
                                                                              uint64_t packet_number,
                                                                              uint64_t send_time,
                                                                              uint64_t current_time) {
    protoop_arg_t args[4];
    args[0] = (protoop_arg_t) path;
    args[1] = (protoop_arg_t) packet_number;
    args[2] = (protoop_arg_t) send_time;
    args[3] = (protoop_arg_t) current_time;
    return (int) run_noparam(cnx, PACKET_HAS_BEEN_ACKNOWLEDGED, 4, args, NULL);
}

// not specific to FEC
static __attribute__((always_inline)) int packet_has_been_lost(picoquic_cnx_t *cnx,
                                                                              picoquic_path_t *path,
                                                                              uint64_t packet_number,
                                                                              uint64_t send_time,
                                                                              uint64_t current_time) {
    protoop_arg_t args[4];
    args[0] = (protoop_arg_t) path;
    args[1] = (protoop_arg_t) packet_number;
    args[2] = (protoop_arg_t) send_time;
    args[3] = (protoop_arg_t) current_time;
    return (int) run_noparam(cnx, PACKET_HAS_BEEN_LOST, 4, args, NULL);
}


static __attribute__((always_inline)) int fec_packet_has_been_lost(picoquic_cnx_t *cnx,
                                                                              uint64_t current_time,
                                                                              uint64_t packet_number,
                                                                              uint64_t packet_slot,
                                                                              source_symbol_id_t id,
                                                                              uint16_t n_source_symbols_in_packet,
                                                                              bool fec_protected,
                                                                              bool contains_repair_Frame,
                                                                              uint64_t send_time) {
    protoop_arg_t args[8];
    args[0] = (protoop_arg_t) packet_number;
    args[1] = (protoop_arg_t) packet_slot;
    args[2] = (protoop_arg_t) id;
    args[3] = (protoop_arg_t) n_source_symbols_in_packet;
    args[4] = (protoop_arg_t) fec_protected;
    args[5] = (protoop_arg_t) contains_repair_Frame;
    args[6] = (protoop_arg_t) send_time;
    args[7] = (protoop_arg_t) current_time;
    return (int) run_noparam(cnx, FEC_PACKET_HAS_BEEN_LOST, 8, args, NULL);
}

// the symbols have been received, meaning that it has either been received or recovered
static __attribute__((always_inline)) what_to_send_t fec_packet_symbols_have_been_received(picoquic_cnx_t *cnx,
                                                                                           uint64_t packet_number,
                                                                                           uint64_t packet_slot,
                                                                                           source_symbol_id_t id,
                                                                                           uint16_t n_source_symbols_in_packet,
                                                                                           bool fec_protected,
                                                                                           bool contains_repair_Frame,
                                                                                           uint64_t sent_time,
                                                                                           uint64_t current_time) {
    protoop_arg_t args[8];
    args[0] = (protoop_arg_t) packet_number;
    args[1] = (protoop_arg_t) packet_slot;
    args[2] = (protoop_arg_t) id;
    args[3] = (protoop_arg_t) n_source_symbols_in_packet;
    args[4] = (protoop_arg_t) fec_protected;
    args[5] = (protoop_arg_t) contains_repair_Frame;
    args[6] = (protoop_arg_t) sent_time;
    args[7] = (protoop_arg_t) current_time;
    return (int) run_noparam(cnx, FEC_PACKET_HAVE_BEEN_RECEIVED, 8, args, NULL);
}

static __attribute__((always_inline)) what_to_send_t fec_sent_packet(picoquic_cnx_t *cnx, uint64_t current_time, picoquic_path_t *path, picoquic_packet_t *packet,
                                                                     bool fec_protected, bool contains_repair_frame, bool is_fb_fec) {
    protoop_arg_t args[6];
    args[0] = (protoop_arg_t) packet;
    args[1] = (protoop_arg_t) current_time;
    args[2] = (protoop_arg_t) fec_protected;
    args[3] = (protoop_arg_t) contains_repair_frame;
    args[4] = (protoop_arg_t) is_fb_fec;
    args[5] = (protoop_arg_t) path;
    return (int) run_noparam(cnx, FEC_SENT_PACKET, 6, args, NULL);
}

static __attribute__((always_inline)) what_to_send_t fec_after_incoming_packet(picoquic_cnx_t *cnx) {

    return (int) run_noparam(cnx, FEC_AFTER_INCOMING_PACKET, 0, NULL, NULL);
}


static __attribute__((always_inline)) what_to_send_t fec_has_protected_data_to_send(picoquic_cnx_t *cnx) {

    return (int) run_noparam(cnx, FEC_PROTOOP_HAS_PROTECTED_DATA_TO_SEND, 0, NULL, NULL);
}


// depends on the loss_monitor plugin
static __attribute__((always_inline)) int get_loss_parameters(picoquic_cnx_t *cnx, picoquic_path_t *path, uint64_t current_time, uint64_t granularity,
                                                                         uint64_t *uniform_rate_times_granularity, uint64_t *p_times_granularity,
                                                                         uint64_t *r_times_granularity) {

    protoop_arg_t args[3];
    args[0] = (protoop_arg_t) path;
    args[1] = (protoop_arg_t) granularity;
    args[2] = (protoop_arg_t) current_time;
    protoop_arg_t output[4];
    int retval = (int) run_noparam(cnx, GET_LOSS_PARAMETERS, 3, args, output);
    if (uniform_rate_times_granularity) *uniform_rate_times_granularity = output[0];
    if (p_times_granularity) *p_times_granularity = output[1];
    if (r_times_granularity) *r_times_granularity = output[2];
    return retval;
}

static __attribute__((always_inline)) int reserve_src_fpi_frame(picoquic_cnx_t *cnx, source_symbol_id_t id) {
    reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
    if (!slot)
        return PICOQUIC_ERROR_MEMORY;

    my_memset(slot, 0, sizeof(reserve_frame_slot_t));

    slot->frame_type = FRAME_FEC_SRC_FPI;
    slot->nb_bytes = 1 + MAX_SRC_FPI_SIZE;
    slot->is_congestion_controlled = true;
    slot->low_priority = true;
    slot->frame_ctx = (void *) id;
    if (reserve_frames(cnx, 1, slot) != 1 + MAX_SRC_FPI_SIZE)
        return PICOQUIC_ERROR_MEMORY;
    return 0;
}


typedef struct lost_packet {
    uint64_t pn;
    uint64_t slot;
    source_symbol_id_t id;
    uint16_t n_source_symbols;
    uint64_t send_time;
    struct lost_packet *next;
    struct lost_packet *previous;
} lost_packet_t;

// we use a queue because it is more likely that we dequeue the packets in the order we inserted them
typedef struct lost_packet_queue {
    lost_packet_t *head;
    lost_packet_t *tail;
} lost_packet_queue_t;

// pre: queue != NULL
static __attribute__((always_inline)) int add_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue, uint64_t pn, uint64_t slot, source_symbol_id_t id, uint16_t n_source_symbols, uint64_t send_time) {
    lost_packet_t *lp = my_malloc(cnx, sizeof(lost_packet_t));
    if (!lp) {
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(lp, 0, sizeof(lost_packet_t));
    lp->pn = pn;
    lp->slot = slot;
    lp->id = id;
    lp->n_source_symbols = n_source_symbols;
    lp->send_time = send_time;
    if (!queue->head && !queue->tail) {
        queue->head = queue->tail = lp;
        return 0;
    }
    queue->tail->next = lp;
    lp->previous = queue->tail;
    queue->tail = lp;
    return 0;
}

// sets *slot to the slot when the packet was sent, dequeues the packet from the queue and returns true if this packet was present
// if the packet was not present, does nothing and returns false
static __attribute__((always_inline)) bool dequeue_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue, uint64_t pn, uint64_t *slot, source_symbol_id_t *id, uint16_t *n_source_symbols, uint64_t *send_time) {
    if (!queue->head && !queue->tail)
        return false;
    lost_packet_t *previous = NULL;
    lost_packet_t *current = queue->head;
    while (current) {
        if (current->pn == pn) {
            if (slot)
                *slot = current->slot;
            if (id)
                *id = current->id;
            if (n_source_symbols)
                *n_source_symbols = current->n_source_symbols;
            if (send_time)
                *send_time = current->send_time;
            if (!previous) {
                // head == current
                queue->head = current->next;
                if (queue->head)
                    queue->head->previous = NULL;
            } else {
                previous->next = current->next;
                if (current->next)
                    current->next->previous = previous;
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
static __attribute__((always_inline)) lost_packet_t *get_first_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue) {
    if (!queue->head && !queue->tail)
        return NULL;
    return queue->head;
}

// returns the packet number at first position of the queue (i.e. the oldest enqueued packet)
// returns -1 if the queue is empty
static __attribute__((always_inline)) lost_packet_t *get_last_lost_packet(picoquic_cnx_t *cnx, lost_packet_queue_t *queue) {
    if (!queue->head && !queue->tail)
        return NULL;
    return queue->tail;
}


// returns the packet number at first position of the queue (i.e. the oldest enqueued packet)
// returns -1 if the queue is empty
static __attribute__((always_inline)) lost_packet_t *get_lost_packet_equal_or_smaller(picoquic_cnx_t *cnx, lost_packet_queue_t *queue, uint64_t pn) {
    if (!queue->head && !queue->tail)
        return NULL;
    lost_packet_t *current = queue->tail;
    while (current && current->pn > pn) {
        current = current->previous;
    }
    return current;
}

static __attribute__((always_inline)) bool is_lost_packet_queue_empty(picoquic_cnx_t *cnx, lost_packet_queue_t *queue) {
    return !queue->head || !queue->tail;
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




#define for_each_source_symbol(____sss, ____ss, ____nss) \
    for (int ____i = 0, ____keep = 1, n = ____nsss; ____keep && ____i < n; ____i++, ____keep = 1-____keep ) \
        for (____ss = ____sss[____i] ; ____keep ; ____keep = 1-____keep)

#define for_each_repair_symbol(____rss, ____rs, ____nrs) \
    for (int ____i = 0, ____keep = 1, n = ____nrs; ____keep && ____i < n; ____i++, ____keep = 1-____keep ) \
        for (____rs = ____rss[____i] ; ____keep ; ____keep = 1-____keep)







#endif //FEC_UTILS_H
