#ifndef CAUSAL_REDUNDANCY_CONTROLLER_H
#define CAUSAL_REDUNDANCY_CONTROLLER_H
#include "picoquic.h"
#include "../fec.h"
#include "../window_framework/types.h"
#include "../window_framework/framework_sender.h"


// TODO: 1 packet == 1 symbol here, or it is too complicated
#define MAX_SLOTS MAX_SENDING_WINDOW_SIZE

#define GRANULARITY 1000000

#define NUMBER_OF_SOURCE_SYMBOLS_TO_PROTECT_IN_FB_FEC 2

typedef uint64_t buffer_elem_t;

typedef enum causal_packet_type {
    new_rlnc_packet,
    fec_packet,
    fb_fec_packet,
    nothing,
} causal_packet_type_t;

typedef struct {
    buffer_elem_t *elems;
    int max_size;
    int current_size;
    int start;
} buffer_t;

typedef union {
    fec_window_t window;
    buffer_elem_t elem;
} rlnc_window_t;


#define DEADLINE_CRITICAL_THRESHOLD_MICROSEC 25000  // because I had to choose a value

// packed needed because of the malloc block size...
typedef struct __attribute__((__packed__)) {
    int64_t slot;
    window_packet_metadata_t md;
} history_entry_t;

typedef struct {
    history_entry_t *sent_windows;
    int max_size;
} slots_history_t;

typedef struct {
    buffer_t *acked_slots;
    buffer_t *nacked_slots;
    buffer_t *received_symbols;
    buffer_t *fec_slots;
    buffer_t *fb_fec_slots;
    buffer_t *what_to_send;
    buffer_t *lost_and_non_fec_retransmitted_slots;
    slots_history_t *slots_history;
    int64_t ad, md, n_lost_slots;
    int64_t d_times_granularity;
    window_source_symbol_id_t latest_symbol_protected_by_fec;
    window_source_symbol_id_t latest_symbol_when_fec_scheduled;
    uint32_t m;
    uint64_t n_received_feedbacks;
    available_slot_reason_t last_feedback;
    // FIXME: assumes single-path context, we should have 1 sample point per path
    uint64_t last_bandwidth_check_timestamp_microsec;
    uint64_t n_bytes_sent_since_last_bandwidth_check;
    uint64_t last_bytes_sent_sample;
    uint64_t last_bytes_sent_sampling_period_microsec;

    bool flush_dof_mode;
    int64_t n_fec_in_flight;

    protoop_arg_t causal_addons_states[10];

} causal_redundancy_controller_t;

// bandwidth defines a bandwidth metric
// should only be used relatively (ratio) to another bandwidth
typedef uint64_t bandwidth_t;

static __attribute__((always_inline)) buffer_t *create_buffer(picoquic_cnx_t *cnx, int max_size) {
    if (max_size > MAX_SLOTS || !max_size) return NULL;
    buffer_elem_t *elems = my_malloc(cnx, max_size*sizeof(buffer_elem_t));
    if (!elems) return NULL;
    my_memset(elems, 0, max_size*sizeof(buffer_elem_t));
    buffer_t *buffer = my_malloc(cnx, sizeof(buffer_t));
    if (!buffer) {
        my_free(cnx, elems);
        return NULL;
    }
    my_memset(buffer, 0, sizeof(buffer_t));
    buffer->max_size = max_size;
    buffer->elems = elems;
    return buffer;
}
static __attribute__((always_inline)) void destroy_buffer(picoquic_cnx_t *cnx, buffer_t *buffer) {
    my_free(cnx, buffer->elems);
    my_free(cnx, buffer);
}



static __attribute__((always_inline)) void add_elem_to_buffer(buffer_t *buffer, buffer_elem_t slot) {
    uint64_t idx = buffer->start + buffer->current_size;
    idx = idx % (uint64_t) buffer->max_size;
    buffer->elems[idx] = slot;
    if (buffer->current_size == buffer->max_size) {
        buffer->start = ((uint64_t)(buffer->start + 1)) % ((uint64_t)buffer->max_size);
    } else {
        buffer->current_size++;
    }
}
static __attribute__((always_inline)) void add_elem_to_buffer2(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, buffer_t *buffer, buffer_elem_t slot) {
    uint64_t idx = buffer->start + buffer->current_size;
    idx = idx % (uint64_t) buffer->max_size;
    *(buffer->elems + idx) = slot;
    if (buffer->current_size == buffer->max_size) {
        buffer->start = ((uint64_t)(buffer->start + 1)) % ((uint64_t)buffer->max_size);
    } else {
        buffer->current_size++;
    }
}


static __attribute__((always_inline)) bool remove_elem_from_buffer(buffer_t *buffer, buffer_elem_t slot) {
    if (buffer->current_size == 0) return false;
    if (buffer->elems[buffer->start] == slot) {
        buffer->start = ((uint64_t)(buffer->start + 1)) % ((uint64_t)buffer->max_size);
        buffer->current_size--;
        return true;
    }
    return false;
}

static __attribute__((always_inline)) bool dequeue_elem_from_buffer(buffer_t *buffer, buffer_elem_t *elem) {
    if (buffer->current_size == 0) return false;
    *elem = buffer->elems[buffer->start];
    buffer->start = ((uint64_t)(buffer->start + 1)) % ((uint64_t)buffer->max_size);
    buffer->current_size--;
    return true;
}

static __attribute__((always_inline)) bool is_elem_in_buffer(buffer_t *buffer, buffer_elem_t slot) {
    for (int i = 0 ; i < buffer->current_size ; i++) {
        if (buffer->elems[((uint64_t)(buffer->start + i)) % ((uint64_t)buffer->max_size)] == slot)
            return true;
    }
    return false;
}



static __attribute__((always_inline)) bool is_buffer_empty(buffer_t *buffer) {
    return buffer->current_size == 0;
}






static __attribute__((always_inline)) bool is_symbol_in_window(fec_window_t *window, window_source_symbol_id_t id) {
    return window->start <= id && id < window->end;
}

static __attribute__((always_inline)) bool is_window_empty(fec_window_t *window) {
    return window->start >= window->end;
}

static __attribute__((always_inline)) int64_t window_size(fec_window_t *window) {
    return window->end - window->start;
}


// returns true if the intersection of these  windows is not empty, false otherwise
static __attribute__((always_inline)) bool window_intersects(fec_window_t *window1, fec_window_t *window2) {
    fec_window_t *tmp = window1;
    if (window1->start > window2->start) {
        window1 = window2;
        window2 = tmp;
    }
    // now we are sure that window1->start <= window2->start
    return !is_window_empty(window1) && !is_window_empty(window2) && window1->end > window2->start;
}

static __attribute__((always_inline)) bool remove_symbol_from_window(fec_window_t *window, window_source_symbol_id_t id) {
    if (!is_symbol_in_window(window, id) || is_window_empty(window) || window->start != id) return false;
    window->start++;
    return true;
}




static __attribute__((always_inline)) slots_history_t *create_history(picoquic_cnx_t *cnx, int max_size) {
    if (max_size > MAX_SLOTS || !max_size) return NULL;
    history_entry_t *windows = my_malloc(cnx, max_size*sizeof(history_entry_t));
    if (!windows) return NULL;
    my_memset(windows, 0, max_size*sizeof(buffer_elem_t));
    slots_history_t *history = my_malloc(cnx, sizeof(slots_history_t));
    if (!history) {
        my_free(cnx, windows);
        return NULL;
    }
    my_memset(history, 0, sizeof(buffer_t));
    history->max_size = max_size;
    history->sent_windows = windows;
    return history;
}
static __attribute__((always_inline)) void destroy_history(picoquic_cnx_t *cnx, slots_history_t *history) {
    my_free(cnx, history->sent_windows);
    my_free(cnx, history);
}



static __attribute__((always_inline)) void add_elem_to_history(slots_history_t *history, int64_t slot, window_packet_metadata_t md) {
    history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].slot = slot;
    history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].md = md;
}


static __attribute__((always_inline)) bool remove_elem_from_history(slots_history_t *history, int64_t slot) {
    history_entry_t *entry = &history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)];
    if (entry->slot == slot) {
        entry->slot = -1;
//        entry->window.start = 0;
//        entry->window.end = 0;
        return true;
    }
    return false;
}

static __attribute__((always_inline)) bool is_slot_in_history(slots_history_t *history, int64_t slot) {
    return history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].slot == slot;
}

// returns -1 if the slot is not in the history
static __attribute__((always_inline)) int get_window_sent_at_slot(causal_redundancy_controller_t *controller, slots_history_t *history, int64_t slot, fec_window_t *w) {
    if (!is_slot_in_history(history, slot)) return -1;
    window_packet_metadata_t md = history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].md;
    if(is_elem_in_buffer(controller->fec_slots, slot)
       || is_elem_in_buffer(controller->fb_fec_slots, slot)) {
        w->start = md.repair_metadata.first_protected_source_symbol_id;
        w->end = w->start + md.repair_metadata.n_protected_source_symbols;
    } else {
        w->start = md.source_metadata.first_symbol_id;
        w->end = w->start + md.source_metadata.number_of_symbols;
    }
    return 0;
}

static __attribute__((always_inline)) int history_get_sent_slot_metadata(slots_history_t *history, int64_t slot,
                                                                         window_packet_metadata_t *md) {
    if (!is_slot_in_history(history, slot)) return -1;
    *md = history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].md;
    return 0;
}



static __attribute__((always_inline)) causal_redundancy_controller_t *create_causal_redundancy_controller(picoquic_cnx_t *cnx, uint32_t m) {
    causal_redundancy_controller_t *controller = my_malloc(cnx, sizeof(causal_redundancy_controller_t));
    if (!controller) return NULL;
    memset(controller, 0, sizeof(causal_redundancy_controller_t));
    controller->m = m;
    controller->acked_slots = create_buffer(cnx, MAX_SLOTS);
    if (!controller->acked_slots) {
        my_free(cnx, controller);
    }
    controller->nacked_slots = create_buffer(cnx, MAX_SLOTS);
    if (!controller->nacked_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        my_free(cnx, controller);
    }
    controller->received_symbols = create_buffer(cnx, MAX_SLOTS);
    if (!controller->received_symbols) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        my_free(cnx, controller);
    }
    controller->fec_slots = create_buffer(cnx, MAX_SLOTS);
    if (!controller->fec_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        my_free(cnx, controller);
    }
    controller->fb_fec_slots = create_buffer(cnx, MAX_SLOTS);
    if (!controller->fb_fec_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        my_free(cnx, controller);
    }
    controller->what_to_send = create_buffer(cnx, MAX_SLOTS);
    if (!controller->what_to_send) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        destroy_buffer(cnx, controller->fb_fec_slots);
        my_free(cnx, controller);
    }
    controller->lost_and_non_fec_retransmitted_slots = create_buffer(cnx, MAX_SLOTS);
    if (!controller->lost_and_non_fec_retransmitted_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        destroy_buffer(cnx, controller->fb_fec_slots);
        destroy_buffer(cnx, controller->what_to_send);
        my_free(cnx, controller);
    }
    controller->slots_history = create_history(cnx, MAX_SLOTS);
    if (!controller->slots_history) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        destroy_buffer(cnx, controller->fb_fec_slots);
        destroy_buffer(cnx, controller->what_to_send);
        destroy_buffer(cnx, controller->lost_and_non_fec_retransmitted_slots);
        my_free(cnx, controller);
    }
    return controller;
}

static __attribute__((always_inline)) void destroy_causal_redundancy_controller(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller) {
    destroy_buffer(cnx, controller->acked_slots);
    destroy_buffer(cnx, controller->nacked_slots);
    my_free(cnx, controller);
}

static __attribute__((always_inline)) int64_t r_times_granularity(causal_redundancy_controller_t *controller) {
    if (controller->n_received_feedbacks == 0)
        return GRANULARITY;
    return GRANULARITY - ((uint64_t)(controller->n_lost_slots*GRANULARITY) / ((uint64_t)controller->n_received_feedbacks));
}

static __attribute__((always_inline)) bool EW(picoquic_cnx_t *cnx, picoquic_path_t *path, causal_redundancy_controller_t *controller, uint64_t granularity, fec_window_t *current_window, uint64_t current_time) {

    fec_window_t *malloced_window = my_malloc(cnx, sizeof(fec_window_t));
    if (!malloced_window) {
        return PICOQUIC_ERROR_MEMORY;
    }
    *malloced_window = *current_window;

    protoop_arg_t args[5];
    args[0] = (protoop_arg_t) path;
    args[1] = (protoop_arg_t) controller;
    args[2] = (protoop_arg_t) granularity;
    args[3] = (protoop_arg_t) malloced_window;
    args[4] = (protoop_arg_t) current_time;
    int retval =  run_noparam(cnx, "causal_ew", 5, args, NULL);
    my_free(cnx, malloced_window);
    return retval;
}

static __attribute__((always_inline)) int64_t threshold(picoquic_cnx_t *cnx, picoquic_path_t *path, causal_redundancy_controller_t *controller, uint64_t granularity, uint64_t current_time) {

    protoop_arg_t args[4];
    args[0] = (protoop_arg_t) path;
    args[1] = (protoop_arg_t) controller;
    args[2] = (protoop_arg_t) granularity;
    args[3] = (protoop_arg_t) current_time;
    return run_noparam(cnx, "causal_threshold", 4, args, NULL);
}

static __attribute__((always_inline)) bool below_threshold(picoquic_cnx_t *cnx, picoquic_path_t *path, causal_redundancy_controller_t *controller, uint64_t granularity, uint64_t current_time) {
    int64_t th = threshold(cnx, path, controller, granularity, current_time);
    int64_t diff = (r_times_granularity(controller) - controller->d_times_granularity) - th;
    diff = diff > 0 ? diff : -diff;
    // handling precision errors
    PROTOOP_PRINTF(cnx, "%ld - %ld <? (%ld) => %d\n", r_times_granularity(controller), controller->d_times_granularity, th, r_times_granularity(controller) - controller->d_times_granularity < th);
    if (diff < 2){
        return false;
    }
    PROTOOP_PRINTF(cnx, "%ld - %ld <? (%ld) => %d\n", r_times_granularity(controller), controller->d_times_granularity, th, r_times_granularity(controller) - controller->d_times_granularity < th);
// old way:
    return r_times_granularity(controller) - controller->d_times_granularity < th;
}


static __attribute__((always_inline)) uint32_t compute_ad(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, fec_window_t *current_window) {
    uint32_t ad = 0;
    int err = 0;
    fec_window_t sent_window;
    for (int i = 0 ; i < controller->fec_slots->current_size ; i++) {
        uint64_t slot = controller->fec_slots->elems[((uint64_t)(controller->fec_slots->start + i))  % ((uint64_t)controller->fec_slots->max_size)];
        err = get_window_sent_at_slot(controller, controller->slots_history, slot, &sent_window);

//        PROTOOP_PRINTF(cnx, "WINDOW SENT AT SLOT %lu = [%u, %u[\n", slot, sent_window.start, sent_window.end);
        if (!err && window_intersects(&sent_window, current_window) && !is_elem_in_buffer(controller->nacked_slots, slot) && (!is_elem_in_buffer(controller->acked_slots, slot))) {
            ad++;
        }
    }
    for (int i = 0 ; i < controller->fb_fec_slots->current_size ; i++) {
        uint64_t slot = controller->fb_fec_slots->elems[((uint64_t)(controller->fb_fec_slots->start + i)) % ((uint64_t)controller->fb_fec_slots->max_size)];
        err = get_window_sent_at_slot(controller, controller->slots_history, slot, &sent_window);
        if (!err && window_intersects(&sent_window, current_window) && !is_elem_in_buffer(controller->nacked_slots, slot) && (!is_elem_in_buffer(controller->acked_slots, slot))) ad++;
    }
    PROTOOP_PRINTF(cnx, "RETURN AD = %u\n", ad);
    return ad;
}

static __attribute__((always_inline)) uint32_t compute_md(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, fec_window_t *current_window) {
    uint32_t md = 0;
    for (int i = 0 ; i < controller->nacked_slots->current_size ; i++) {
        uint64_t slot = controller->nacked_slots->elems[((uint64_t)(controller->nacked_slots->start + i)) % ((uint64_t)controller->nacked_slots->max_size)];
        fec_window_t sent_window;
        int err = get_window_sent_at_slot(controller, controller->slots_history, slot, &sent_window);
        // see equation
        if (!err
            && !is_elem_in_buffer(controller->fec_slots, slot)
            && !is_elem_in_buffer(controller->fb_fec_slots, slot)
            && window_intersects(&sent_window, current_window))
            md++;
    }
    PROTOOP_PRINTF(cnx, "RETURN MD = %u\n", md);
    return md;
}

static __attribute__((always_inline)) causal_packet_type_t what_to_send(picoquic_cnx_t *cnx, window_redundancy_controller_t c, window_source_symbol_id_t *first_id_to_protect, uint16_t *n_symbols_to_protect, fec_window_t *window) {
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) c;
    if (is_buffer_empty(controller->what_to_send))
        return nothing;
    buffer_elem_t type;
    dequeue_elem_from_buffer(controller->what_to_send, &type);
    if (type == fb_fec_packet) {
            uint64_t lost_slot = 0;
            bool dequeued = false;
            while((dequeued = dequeue_elem_from_buffer(controller->lost_and_non_fec_retransmitted_slots, &lost_slot))) {
                // pass, the slot is too old, this means that it has been recovered at some point
                if (is_slot_in_history(controller->slots_history, lost_slot)) {
                    window_packet_metadata_t md;
                    history_get_sent_slot_metadata(controller->slots_history, lost_slot, &md);
                    // in this redundancy controller, we assume that 1 packet == 1 source/repair symbol
                    if ((md.source_metadata.number_of_symbols > 0 &&
                            ((md.source_metadata.first_symbol_id + md.source_metadata.number_of_symbols - 1) >= window->start))
                        ||(md.repair_metadata.number_of_repair_symbols > 0 &&
                            ((md.repair_metadata.first_protected_source_symbol_id + md.repair_metadata.number_of_repair_symbols - 1) >= window->start))) {
                        // outdated compared to the window
                        break;
                    }

                }
            }


            if (dequeued) {
                PROTOOP_PRINTF(cnx, "GOT SLOT %lu AS LOST AND NON RETRANSMITTED\n", lost_slot);
                window_packet_metadata_t md;
                int err = history_get_sent_slot_metadata(controller->slots_history, lost_slot, &md);
                if (err == -1) {
                    // should never happen
                    PROTOOP_PRINTF(cnx, "ERROR: ASKED TO RETRANSMIT A NON-SENT SLOT\n");
                    return new_rlnc_packet;
                }
                // in this redundancy controller, we assume that 1 packet == 1 source/repair symbol
                if (md.source_metadata.number_of_symbols > 0) {
                    // this slot transported a source symbol
                    *first_id_to_protect = md.source_metadata.first_symbol_id;
                    *n_symbols_to_protect = NUMBER_OF_SOURCE_SYMBOLS_TO_PROTECT_IN_FB_FEC;
                    PROTOOP_PRINTF(cnx, "FIRST ID IN LOST SLOT ID %u\n", *first_id_to_protect);
                }
                else if (md.repair_metadata.number_of_repair_symbols > 0) {
                    *first_id_to_protect = md.repair_metadata.first_protected_source_symbol_id;
                    *n_symbols_to_protect = md.repair_metadata.n_protected_source_symbols;
                } else {
                    PROTOOP_PRINTF(cnx, "ERROR: 0 SOURCE AND REPAIR SYMBOL IN THE LOST SLOT !!\n");
                }
            } else {
                type = fec_packet;
            }
//            dequeue_elem_from_buffer(controller->lost_and_non_fec_retransmitted_slots, &lost_slot);
        if (type == fec_packet) {
            // fb-fec when there is no lost and non retransmitted packet should be considered as FEC
            controller->n_fec_in_flight++;
        }
    }
    PROTOOP_PRINTF(cnx, "EVENT::{\"time\": %ld, \"type\": \"what_to_send\", \"wts\": %d, \"latest\": %u}\n", picoquic_current_time(), type, controller->latest_symbol_protected_by_fec);
    return type;
}


static __attribute__((always_inline)) void sent_packet(picoquic_cnx_t *cnx, picoquic_path_t *path, window_redundancy_controller_t c, causal_packet_type_t type, uint64_t slot, window_packet_metadata_t md) {
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) c;
    add_elem_to_history(controller->slots_history, slot, md);
    uint64_t n_bytes_sent = 0;
    fec_window_t window;
    if (type == new_rlnc_packet) {
        window.start = md.source_metadata.first_symbol_id;
        window.end = window.start + md.source_metadata.number_of_symbols;
        n_bytes_sent = md.source_metadata.number_of_symbols*SYMBOL_SIZE;
    } else {
        window.start = md.repair_metadata.first_protected_source_symbol_id;
        window.end = window.start + md.repair_metadata.n_protected_source_symbols;
        n_bytes_sent = md.repair_metadata.number_of_repair_symbols*SYMBOL_SIZE;
    }

    uint64_t now = picoquic_current_time();
    if (now - controller->last_bandwidth_check_timestamp_microsec > get_path(path, AK_PATH_SMOOTHED_RTT, 0)) {
        controller->last_bytes_sent_sampling_period_microsec = now - controller->last_bandwidth_check_timestamp_microsec;
        controller->last_bytes_sent_sample = controller->n_bytes_sent_since_last_bandwidth_check;
        controller->n_bytes_sent_since_last_bandwidth_check = 0;
        controller->last_bandwidth_check_timestamp_microsec = now;
    }

    controller->n_bytes_sent_since_last_bandwidth_check += n_bytes_sent;
    PROTOOP_PRINTF(cnx, "SENT PACKET, TYPE = %d, SLOT %lu, %lu FEC IN FLIGHT\n", type, slot, controller->n_fec_in_flight);
    if (type == fec_packet){
        PROTOOP_PRINTF(cnx, "SENT FEC IN SLOT %lu\n", slot);
        controller->latest_symbol_protected_by_fec = window.end-1;
    }
    switch(type) {
        case fec_packet:
            add_elem_to_buffer(controller->fec_slots, slot);
            break;
        case fb_fec_packet:
            add_elem_to_buffer(controller->fb_fec_slots, slot);
            break;
        default:
            break;
    }
}

#define SECOND_IN_MICROSEC 1000000

// either acked or recovered
static __attribute__((always_inline)) void run_algo(picoquic_cnx_t *cnx, picoquic_path_t *path, causal_redundancy_controller_t *controller, available_slot_reason_t feedback, fec_window_t *current_window) {

    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        PROTOOP_PRINTF(cnx, "MEMORY ERROR\n");
        return;
    }
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    PROTOOP_PRINTF(cnx, "CURRENT WINDOW = [%u, %u[, N FEC IN FLIGHT = %lu\n", current_window->start, current_window->end, controller->n_fec_in_flight);
    uint64_t cwin_max_slots = get_path((picoquic_path_t *) path, AK_PATH_CWIN, 0)/get_path((picoquic_path_t *) path, AK_PATH_SEND_MTU, 0);
    int64_t smoothed_rtt_microsec = get_path(path, AK_PATH_SMOOTHED_RTT, 0);
    // FIXME: wrap-around when the sampling period or bytes sent are too high
    bandwidth_t available_bandwidth_bytes_per_second = get_path((picoquic_path_t *) path, AK_PATH_CWIN, 0)*SECOND_IN_MICROSEC/smoothed_rtt_microsec;
    // we take the between the last sampling point and the current bandwidth induced by the bytes in flight
//    bandwidth_t used_bandwidth_bytes_per_second = MAX(controller->last_bytes_sent_sample*SECOND_IN_MICROSEC/controller->last_bytes_sent_sampling_period_microsec,
//                                                        get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0)*SECOND_IN_MICROSEC/get_path(path, AK_PATH_SMOOTHED_RTT, 0));
    bandwidth_t used_bandwidth_bytes_per_second = get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0)*SECOND_IN_MICROSEC/smoothed_rtt_microsec;
    int64_t bw_ratio_times_granularity = (used_bandwidth_bytes_per_second > 0) ? ((GRANULARITY*available_bandwidth_bytes_per_second)/used_bandwidth_bytes_per_second) : 0;
    // FIXME: experimental: set k according to the cwin, but bound it to the buffer length: we cannot store more than MAX_SENDING_WINDOW_SIZE, and sometimes the window length will be 2*k


//    controller->k = 4;//MIN(MAX_SENDING_WINDOW_SIZE/2 - 1, 1 + cwin_max_slots);
    controller->md = compute_md(cnx, controller, current_window);
    controller->ad = compute_ad(cnx, controller, current_window);
    if (controller->md == 0 && controller->ad == 0) {
        controller->d_times_granularity = 0;
    } else if (controller->ad == 0) {
        // no added degrees, but there are missing degrees: the ratio is infinite, we need to retransmit asap
        controller->d_times_granularity = INT64_MAX;
    } else {
        controller->d_times_granularity = ((((uint64_t) controller->md*GRANULARITY))/((uint64_t) (controller->ad)));
    }
//    PROTOOP_PRINTF(cnx, "USED BW = %lu, BW_RATIO = %lu, RECEIVE RATE = %lu, DOF_RATIO = %lu/%lu = %lu\n", used_bandwidth_bytes_per_second, GRANULARITY*available_bandwidth_bytes_per_second/used_bandwidth_bytes_per_second, r_times_granularity(controller), controller->md, ad, controller->d_times_granularity);
    bool added_new_packet = false;
    int i;
    if (is_buffer_empty(controller->what_to_send)) {
        if (controller->flush_dof_mode) {
            PROTOOP_PRINTF(cnx, "FLUSH DOF !!\n");
            add_elem_to_buffer(controller->what_to_send, fec_packet);
            controller->n_fec_in_flight++;
            controller->ad++;
        } else {

//                PROTOOP_PRINTF(cnx, "EVENT::{\"time\": %ld, \"type\": \"algo\", \"feedback\": %d, \"ad\": %ld, \"md\": %ld, \"d\": %ld, \"k\": %ld, \"threshold\": %ld, \"r\": %ld}\n", picoquic_current_time(), controller->last_feedback, controller->ad, controller->md, controller->d_times_granularity, controller->k, controller->threshold_times_granularity, r_times_granularity(controller));


            uint64_t current_time = picoquic_current_time();

            switch (controller->last_feedback) {
                case available_slot_reason_none:
//                        if (false && EW(controller, current_window)) {
//                        if (allowed_to_send_fec_given_deadlines && ((normal_causal && EW(controller, current_window)) || (!fec_has_protected_data_to_send(cnx) && bw_ratio_times_granularity > (GRANULARITY + GRANULARITY/10)))) {
                    if (EW(cnx, path, controller, GRANULARITY, current_window, current_time)) {
                        for (i = 0; i < controller->m; i++) {
                            add_elem_to_buffer(controller->what_to_send, fec_packet);
                            controller->n_fec_in_flight++;
                        }
                        controller->ad += controller->m;
                    } else {

                        // TODO: first check if new data are available to send ?
                        added_new_packet = true;
                        add_elem_to_buffer(controller->what_to_send, new_rlnc_packet);
                    }
                    break;
                case available_slot_reason_nack:
                    if (!below_threshold(cnx, path, controller, GRANULARITY, current_time)) {
                        if (!EW(cnx, path, controller, GRANULARITY, current_window, current_time)) {
                            // TODO: first check if new data are available to send ?
                            added_new_packet = true;
                            add_elem_to_buffer(controller->what_to_send, new_rlnc_packet);
                        } else {
                            for (i = 0; i < controller->m; i++) {
                                add_elem_to_buffer(controller->what_to_send, fec_packet);
                                controller->n_fec_in_flight++;
                            }
                            controller->ad += controller->m;
                        }
                    } else {
                        add_elem_to_buffer(controller->what_to_send, fb_fec_packet);
                        controller->ad++;
                        // below not necessary I think
//                        if (EW(cnx, controller, current_window)) {
//                            for (i = 0; i < controller->m; i++) {
//                                add_elem_to_buffer(controller->what_to_send, fec_packet);
//                                controller->n_fec_in_flight++;
//                                compute_new_last_fully_protected_message_deadline(cnx, wff, controller, current_window);
//                            }
//                            controller->ad += controller->m;
//                        }
                    }
                    break;
                case available_slot_reason_ack:;
                    uint64_t gemodel_r_times_granularity = 1*GRANULARITY;
                    get_loss_parameters(cnx, path, current_time, GRANULARITY, NULL, NULL, &gemodel_r_times_granularity);

//                    if (normal_causal && ((false && EW(controller, current_window) && controller->latest_symbol_when_fec_scheduled != current_window->end-1)
//                                            || (!fec_has_protected_data_to_send(cnx) && (controller->latest_symbol_when_fec_scheduled != current_window->end-1
//                                                                                     || GRANULARITY/MAX(1, GRANULARITY-gemodel_r_times_granularity))))) {
                    if (EW(cnx, path, controller, GRANULARITY, current_window, current_time)) {
                        PROTOOP_PRINTF(cnx, "EW !\n");
                        for (i = 0; i < controller->m; i++) {
                            add_elem_to_buffer(controller->what_to_send, fec_packet);
                            controller->n_fec_in_flight++;
                        }
                        controller->ad += controller->m;
                        controller->latest_symbol_when_fec_scheduled = current_window->end-1;
                    } else {
                        if (below_threshold(cnx, path, controller, GRANULARITY, current_time)) {
                            PROTOOP_PRINTF(cnx, "ADD FEC\n");
                            add_elem_to_buffer(controller->what_to_send, fb_fec_packet);
//                                controller->n_fec_in_flight++;
                            controller->ad++;
                        } else {
                            if (EW(cnx, path, controller, GRANULARITY, current_window, current_time)) {
                                for (i = 0; i < controller->m; i++) {
                                    add_elem_to_buffer(controller->what_to_send, fec_packet);
                                    controller->n_fec_in_flight++;
                                }
                                controller->ad += controller->m;
                            } else {
                                added_new_packet = true;
                                add_elem_to_buffer(controller->what_to_send, new_rlnc_packet);
                            }
                        }
                    }
                    break;
                default:
                    break;
            }

        }
        // we must take the newly added packet into account
        int to_add = added_new_packet ? 1 : 0;
        // TODO: see if this makes sense replacing 2*k by max_window_size
        controller->flush_dof_mode = window_size(current_window) + to_add > MAX_SLOTS-1;//2*controller->k;
    } else {
        PROTOOP_PRINTF(cnx, "BUFFER NOT EMPTY\n");

    }
}

static __attribute__((always_inline)) void slot_acked(picoquic_cnx_t *cnx, window_redundancy_controller_t c, uint64_t slot) {
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) c;
    if (!is_elem_in_buffer(controller->nacked_slots, slot)) {
        // the packet was detect as lost but received in the end
        controller->n_received_feedbacks++;

        window_packet_metadata_t md;
        int err = history_get_sent_slot_metadata(controller->slots_history, slot, &md);
        if (err) {
            PROTOOP_PRINTF(cnx, "ERROR: NACKED A SLOT NEVER SENT !\n");
            return;
        }
        if (md.repair_metadata.number_of_repair_symbols > 0 && !md.repair_metadata.is_fb_fec) {
            PROTOOP_PRINTF(cnx, "ACKED SLOT %lu WITH %d FEC RS\n", slot, md.repair_metadata.number_of_repair_symbols);
            controller->n_fec_in_flight -= md.repair_metadata.number_of_repair_symbols;
        }
    } else {
        PROTOOP_PRINTF(cnx, "ACKED A SLOT PREVIOUSLY MARKED AS LOST\n");
    }
    controller->last_feedback = available_slot_reason_ack;
    add_elem_to_buffer2(cnx, controller, controller->acked_slots, slot);
}

static __attribute__((always_inline)) void free_slot_without_feedback(picoquic_cnx_t *cnx, window_redundancy_controller_t c) {
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) c;
    controller->last_feedback = available_slot_reason_none;
}

static __attribute__((always_inline)) void slot_nacked(picoquic_cnx_t *cnx, window_redundancy_controller_t c, uint64_t slot) {
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) c;
    controller->n_received_feedbacks++;
    controller->n_lost_slots++;
    window_packet_metadata_t md;
    int err = history_get_sent_slot_metadata(controller->slots_history, slot, &md);
    if (err) {
        PROTOOP_PRINTF(cnx, "ERROR: NACKED A SLOT NEVER SENT !\n");
        return;
    }
    if (md.source_metadata.number_of_symbols > 0  || (md.repair_metadata.is_fb_fec)) {
        PROTOOP_PRINTF(cnx, "ADD SLOT %lu AS LOST AND NON RETRANSMITTED\n", slot);
        add_elem_to_buffer(controller->lost_and_non_fec_retransmitted_slots, slot);
    }
    if (md.repair_metadata.number_of_repair_symbols > 0 && !md.repair_metadata.is_fb_fec) {
        controller->n_fec_in_flight -= md.repair_metadata.number_of_repair_symbols;
    }
    // remove all the acked slots in the window
    add_elem_to_buffer(controller->nacked_slots, slot);
    controller->last_feedback = available_slot_reason_nack;

}
#endif