#ifndef CAUSAL_REDUNDANCY_CONTROLLER_H
#define CAUSAL_REDUNDANCY_CONTROLLER_H
#include "picoquic.h"
#include "../fec.h"
#include "../window_framework/types.h"


#define MAX_SLOTS 100

#define GRANULARITY 1000000

typedef uint64_t buffer_elem_t;

typedef enum causal_packet_type {
    new_rlnc_packet,
    fec_packet,
    fb_fec_packet,
    nothing,
} causal_packet_type_t;

typedef enum causal_feedback {
    no_feedback,
    ack_feedback,
    nack_feedback,
} causal_feedback_t;

typedef struct {
    buffer_elem_t *elems;
    int max_size;
    int current_size;
    int start;
} buffer_t;

typedef union {
    struct __attribute__((__packed__)) {
        uint32_t start, end;
    };
    buffer_elem_t elem;
} rlnc_window_t;

typedef struct {
    int64_t slot;
    rlnc_window_t window;
} history_entry_t;

typedef struct {
    history_entry_t *sent_windows;
    int max_size;
} slots_history_t;

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
    buffer->elems[((uint64_t)(buffer->start + buffer->current_size)) % ((uint64_t)buffer->max_size)] = slot;
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






static __attribute__((always_inline)) bool is_symbol_in_window(rlnc_window_t *window, window_source_symbol_id_t id) {
    return window->start <= id && id < window->end;
}

static __attribute__((always_inline)) bool is_window_empty(rlnc_window_t *window) {
    return window->start >= window->end;
}

static __attribute__((always_inline)) int64_t window_size(rlnc_window_t *window) {
    return window->end - window->start;
}


// returns true if the intersection of these  windows is not empty, false otherwise
static __attribute__((always_inline)) bool window_intersects(rlnc_window_t *window1, rlnc_window_t *window2) {
    rlnc_window_t *tmp = window1;
    if (window1->start > window2->start) {
        window1 = window2;
        window2 = tmp;
    }
    // now we are sure that window1->start <= window2->start
    return !is_window_empty(window1) && !is_window_empty(window2) && window1->end > window2->start;
}

static __attribute__((always_inline)) bool remove_symbol_from_window(rlnc_window_t *window, window_source_symbol_id_t id) {
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



static __attribute__((always_inline)) void add_elem_to_history(slots_history_t *history, int64_t slot, rlnc_window_t window) {
    history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].slot = slot;
    history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].window = window;
}


static __attribute__((always_inline)) bool remove_elem_from_history(slots_history_t *history, int64_t slot) {
    history_entry_t *entry = &history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)];
    if (entry->slot == slot) {
        entry->slot = -1;
        entry->window.start = 0;
        entry->window.end = 0;
        return true;
    }
    return false;
}

static __attribute__((always_inline)) bool is_slot_in_history(slots_history_t *history, int64_t slot) {
    return history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].slot == slot;
}

// returns NULL if the slot is not in the history
static __attribute__((always_inline)) rlnc_window_t *get_window_sent_at_slot(slots_history_t *history, int64_t slot) {
    if (!is_slot_in_history(history, slot)) return NULL;
    return &history->sent_windows[((uint64_t)slot) % ((uint64_t)history->max_size)].window;
}

static __attribute__((always_inline)) bool is_symbol_protected_in_history(slots_history_t *history, window_source_symbol_id_t id) {
    // TODO: enhance performance
    for (int i = 0 ; i < history->max_size ; i++) {
        if (history->sent_windows[i].slot != -1 && is_symbol_in_window(&history->sent_windows[i].window, id))
            return true;
    }
    return false;
}






typedef struct {
    buffer_t *acked_slots;
    buffer_t *nacked_slots;
    buffer_t *received_symbols;
    buffer_t *fec_slots;
    buffer_t *fb_fec_slots;
    buffer_t *what_to_send;
    slots_history_t *slots_history;
    int64_t ad, md, n_lost_slots;
    int64_t threshold_times_granularity;
    int64_t d_times_granularity;
    window_source_symbol_id_t latest_symbol_protected_by_fec;
    uint32_t k, m;
    uint64_t n_received_feedbacks;
    bool flush_dof_mode;
} causal_redundancy_controller_t;

static __attribute__((always_inline)) causal_redundancy_controller_t *create_causal_redundancy_controller(picoquic_cnx_t *cnx, uint32_t threshold_times_granularity, uint32_t m, uint32_t k) {
    causal_redundancy_controller_t *controller = my_malloc(cnx, sizeof(causal_redundancy_controller_t));
    if (!controller) return NULL;
    memset(controller, 0, sizeof(causal_redundancy_controller_t));
    controller->k = k;
    controller->threshold_times_granularity = threshold_times_granularity;
    controller->m = m;
    controller->acked_slots = create_buffer(cnx, 100);
    if (!controller->acked_slots) {
        my_free(cnx, controller);
    }
    controller->nacked_slots = create_buffer(cnx, 100);
    if (!controller->nacked_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        my_free(cnx, controller);
    }
    controller->received_symbols = create_buffer(cnx, 100);
    if (!controller->received_symbols) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        my_free(cnx, controller);
    }
    controller->fec_slots = create_buffer(cnx, 100);
    if (!controller->fec_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        my_free(cnx, controller);
    }
    controller->fb_fec_slots = create_buffer(cnx, 100);
    if (!controller->fb_fec_slots) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        my_free(cnx, controller);
    }
    controller->what_to_send = create_buffer(cnx, 100);
    if (!controller->what_to_send) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        destroy_buffer(cnx, controller->fb_fec_slots);
        my_free(cnx, controller);
    }
    controller->slots_history = create_history(cnx, 100);
    if (!controller->slots_history) {
        destroy_buffer(cnx, controller->acked_slots);
        destroy_buffer(cnx, controller->nacked_slots);
        destroy_buffer(cnx, controller->received_symbols);
        destroy_buffer(cnx, controller->fec_slots);
        destroy_buffer(cnx, controller->fb_fec_slots);
        destroy_buffer(cnx, controller->what_to_send);
        my_free(cnx, controller);
    }
    return controller;
}

static __attribute__((always_inline)) void destroy_causal_redundancy_controller(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller) {
    destroy_buffer(cnx, controller->acked_slots);
    destroy_buffer(cnx, controller->nacked_slots);
    my_free(cnx, controller);
}

static __attribute__((always_inline)) void sent_window(causal_redundancy_controller_t *controller, rlnc_window_t window, uint64_t slot) {
    add_elem_to_history(controller->slots_history, slot, window);
}

static __attribute__((always_inline)) int64_t r_times_granularity(causal_redundancy_controller_t *controller) {
    return GRANULARITY - ((uint64_t)(controller->n_lost_slots*GRANULARITY) / ((uint64_t)controller->n_received_feedbacks));
}

static __attribute__((always_inline)) bool EW(causal_redundancy_controller_t *controller, rlnc_window_t *current_window) {
    return !is_window_empty(current_window) && ((uint64_t)(current_window->end-1)) % ((uint64_t)controller->k) == 0 && ((int64_t) current_window->end - (int64_t) current_window->start >= controller->k) && (current_window->end-1) - controller->latest_symbol_protected_by_fec >= controller->k;
}

static __attribute__((always_inline)) bool threshold_exceeded(causal_redundancy_controller_t *controller) {
    int64_t diff = (r_times_granularity(controller) - controller->d_times_granularity) - controller->threshold_times_granularity;
    diff = diff > 0 ? diff : -diff;
    // handling precision errors
    if (diff < 2){
        return false;
    }
    return r_times_granularity(controller) - controller->d_times_granularity > controller->threshold_times_granularity;
}

static __attribute__((always_inline)) bool threshold_strictly_greater(causal_redundancy_controller_t *controller) {
    int64_t diff = (r_times_granularity(controller) - controller->d_times_granularity) - controller->threshold_times_granularity;
    diff = diff > 0 ? diff : -diff;
    // handling precision errors
    if (diff < 2){
        return false;
    }
    return r_times_granularity(controller) - controller->d_times_granularity < controller->threshold_times_granularity;
}

static __attribute__((always_inline)) uint32_t compute_ad(causal_redundancy_controller_t *controller, rlnc_window_t *current_window) {
    uint32_t ad = 0;
    for (int i = 0 ; i < controller->fec_slots->current_size ; i++) {
        uint64_t slot = controller->fec_slots->elems[((uint64_t)(controller->fec_slots->start + i))  % ((uint64_t)controller->fec_slots->max_size)];
        rlnc_window_t *sent_window = get_window_sent_at_slot(controller->slots_history, slot);
        if (sent_window && window_intersects(sent_window, current_window)) ad++;
    }
    for (int i = 0 ; i < controller->fb_fec_slots->current_size ; i++) {
        uint64_t slot = controller->fb_fec_slots->elems[((uint64_t)(controller->fb_fec_slots->start + i)) % ((uint64_t)controller->fb_fec_slots->max_size)];
        rlnc_window_t *sent_window = get_window_sent_at_slot(controller->slots_history, slot);
        if (sent_window && window_intersects(sent_window, current_window)) ad++;
    }
    return ad;
}

static __attribute__((always_inline)) uint32_t compute_md(causal_redundancy_controller_t *controller, rlnc_window_t *current_window) {
    uint32_t md = 0;
    for (int i = 0 ; i < controller->nacked_slots->current_size ; i++) {
        uint64_t slot = controller->nacked_slots->elems[((uint64_t)(controller->nacked_slots->start + i)) % ((uint64_t)controller->nacked_slots->max_size)];
        rlnc_window_t *sent_window = get_window_sent_at_slot(controller->slots_history, slot);
        // see equation
        if (sent_window
            && !is_elem_in_buffer(controller->fec_slots, slot)
            && !is_elem_in_buffer(controller->fb_fec_slots, slot)
            && window_intersects(sent_window, current_window))
            md++;
    }
    return md;
}

static __attribute__((always_inline)) causal_packet_type_t what_to_send(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller) {
    if (is_buffer_empty(controller->what_to_send))
        return nothing;
    buffer_elem_t type;
    dequeue_elem_from_buffer(controller->what_to_send, &type);
    return type;
}

// either acked or recovered
static __attribute__((always_inline)) void symbol_received(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, window_source_symbol_id_t symbol_id, uint64_t acked_slot, rlnc_window_t *current_window) {
    // remove all the acked slots in the window
    add_elem_to_buffer(controller->acked_slots, acked_slot);
    add_elem_to_buffer(controller->received_symbols, symbol_id);
    while(remove_symbol_from_window(current_window, symbol_id) && symbol_id++, is_elem_in_buffer(controller->received_symbols,
                                                                                               symbol_id));
    // TODO
}


static __attribute__((always_inline)) void sent_packet(causal_redundancy_controller_t *controller, causal_packet_type_t type, uint64_t slot, rlnc_window_t *window) {
    add_elem_to_history(controller->slots_history, slot, *window);
    if (!EW(controller, window)) {
        controller->latest_symbol_protected_by_fec = (((uint64_t) (window->end-1))/((uint64_t)controller->k))*controller->k;
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

// either acked or recovered
static __attribute__((always_inline)) void run_algo(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, causal_feedback_t feedback, rlnc_window_t *current_window) {
    if (feedback != no_feedback)    controller->n_received_feedbacks++;
    if (feedback == nack_feedback)  {
        controller->n_lost_slots++;
    }
    // FIXME: experimental: set k according to the cwin, but bound it to the buffer length: we cannot store more than MAX_SENDING_WINDOW_SIZE, and sometimes the window length will be 2*k
    controller->k = MIN(MAX_SENDING_WINDOW_SIZE/2 - 1, 1 + (get_path((picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0), AK_PATH_CWIN, 0)/PICOQUIC_MAX_PACKET_SIZE));
    controller->md = compute_md(controller, current_window);
    controller->ad = compute_ad(controller, current_window);
    controller->d_times_granularity = controller->ad != 0 ? (((uint64_t) controller->md*GRANULARITY))/((uint64_t) (controller->ad)) : 0;
    bool added_new_packet = false;
    int i;
    if (true || is_buffer_empty(controller->what_to_send)) {
//        PROTOOP_PRINTF(cnx, "WHAT TO SEND SIZE = %d\n", controller->what_to_send->current_size);
        if (controller->flush_dof_mode) {
            add_elem_to_buffer(controller->what_to_send, fec_packet);
            controller->ad++;
        } else {
            // remove all the acked slots in the window
            switch (feedback) {
                case no_feedback:
                    if (false && EW(controller, current_window)) {
                        PROTOOP_PRINTF(cnx, "EW, SEND FEC\n");
                        for (i = 0; i < controller->m; i++) {
                            add_elem_to_buffer(controller->what_to_send, fec_packet);
                        }
                        controller->ad += controller->m;
                    } else {
                        // TODO: first check if new data are available to send ?
                        added_new_packet = true;
                        add_elem_to_buffer(controller->what_to_send, new_rlnc_packet);
                    }
                    break;
                case nack_feedback:
                    if (false && (controller->d_times_granularity == -1 || threshold_exceeded(controller))) {
                        PROTOOP_PRINTF(cnx, "THRESHOLD EXCEEDED\n");
                        if (true || !EW(controller, current_window)) {
                            PROTOOP_PRINTF(cnx, "NEW RLNC\n");
                            // TODO: first check if new data are available to send ?
                            added_new_packet = true;
                            add_elem_to_buffer(controller->what_to_send, new_rlnc_packet);
                        } else {
                            PROTOOP_PRINTF(cnx, "EW, SEND FEC\n");
                            for (i = 0; i < controller->m; i++) {
                                add_elem_to_buffer(controller->what_to_send, fec_packet);
                            }
                            controller->ad += controller->m;
                        }
                    } else {
                        add_elem_to_buffer(controller->what_to_send, fb_fec_packet);
                        controller->ad++;
                        if (false && EW(controller, current_window)) {
                            for (i = 0; i < controller->m; i++) {
                                add_elem_to_buffer(controller->what_to_send, fec_packet);
                            }
                            controller->ad += controller->m;
                        }
                    }
                    break;
                case ack_feedback:
                    if (false && EW(controller, current_window)) {
                        PROTOOP_PRINTF(cnx, "EW, SEND FEC\n");
                        for (i = 0; i < controller->m; i++) {
                            add_elem_to_buffer(controller->what_to_send, fec_packet);
                        }
                        controller->ad += controller->m;
                    } else {
                        if (threshold_strictly_greater(controller)) {
                            add_elem_to_buffer(controller->what_to_send, fb_fec_packet);
                            controller->ad++;
                        } else {
                            added_new_packet = true;
                            // TODO: first check if new data are available to send ?
                            add_elem_to_buffer(controller->what_to_send, new_rlnc_packet);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        // we must take the newly added packet into account
        int to_add = added_new_packet ? 1 : 0;
        controller->flush_dof_mode = window_size(current_window) + to_add > 2*controller->k;
    } else {
        PROTOOP_PRINTF(cnx, "BUFFER NOT EMPTY\n");

    }
}

static __attribute__((always_inline)) void slot_acked(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, uint64_t slot, rlnc_window_t *current_window) {
    add_elem_to_buffer(controller->acked_slots, slot);
    run_algo(cnx, controller, ack_feedback, current_window);
}

static __attribute__((always_inline)) void free_slot_without_feedback(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, rlnc_window_t *current_window) {
    run_algo(cnx, controller, no_feedback, current_window);
}

static __attribute__((always_inline)) void slot_nacked(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, uint64_t slot, rlnc_window_t *current_window) {
    // remove all the acked slots in the window
    add_elem_to_buffer(controller->nacked_slots, slot);
    run_algo(cnx, controller, nack_feedback, current_window);
}
#endif