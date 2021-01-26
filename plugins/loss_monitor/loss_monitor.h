
#ifndef PICOQUIC_LOSS_MONITOR_H
#define PICOQUIC_LOSS_MONITOR_H

#define PATH_METADATA_LOSS_MONITOR_IDX 1

#include <red_black_tree.h>
#include "../helpers.h"

// a packet monitor has no path notion: it has to be attached to a path
// it only considers the 1-RTT packet number space

typedef struct {
    rbt_node_t *node;
    bool right;
} rbt_node_path_t;

typedef struct {
    rbt_node_path_t nodes[RBT_MAX_DEPTH];
    int current_size;
} rbt_array_stack_t;

typedef enum {
    loss_monitor_packet_event_acknowledged,
    loss_monitor_packet_event_lost
} loss_monitor_packet_event_type_t;

typedef struct {
    loss_monitor_packet_event_type_t event_type;
    int64_t packet_number;
    uint64_t send_time;
} packet_event_t;

typedef struct {
    uint64_t granularity;
    uint64_t p_times_granularity;
    uint64_t r_times_granularity;
    uint64_t uniform_rate_times_granularity;
} loss_monitor_estimations_t;

typedef struct {
    bool has_done_estimations;
    rbt_array_stack_t recursion_helper_stack;
    red_black_tree_t packet_events;
    uint64_t event_lifetime_microsec;
    uint64_t estimations_update_interval;
    uint64_t last_estimation_update_timestamp;
    loss_monitor_estimations_t estimations;
} loss_monitor_t;

#define DEFAULT_ESTIMATION_GRANULARITY 1000000
#define DEFAULT_ESTIMATION_INTERVAL_MICROSEC 200000
#define DEFAULT_EVENT_LIFETIME_MICROSEC 10000000
#define MAX_STORED_EVENTS 5000
#define MIN_STORED_EVENTS_FOR_ESTIMATION 500



static __attribute__((always_inline)) int rbt_stack_init(rbt_array_stack_t *stack) {
    my_memset(stack, 0, sizeof(rbt_array_stack_t));
    return 0;
}

static __attribute__((always_inline)) int rbt_stack_size(rbt_array_stack_t *stack) {
    return stack->current_size;
}

static __attribute__((always_inline)) void rbt_stack_reset(rbt_array_stack_t *stack) {
    stack->current_size = 0;
}

static __attribute__((always_inline)) int rbt_stack_push(rbt_array_stack_t *stack, rbt_node_t *node, bool right) {
    if (!node || stack->current_size == RBT_MAX_DEPTH)
        return -1;
    stack->nodes[stack->current_size++] = (rbt_node_path_t) { .node = node, .right = right };
    return 0;
}

static __attribute__((always_inline)) rbt_node_path_t rbt_stack_pop(rbt_array_stack_t *stack) {
    if (stack->current_size == 0)
        return (rbt_node_path_t) { .node = NULL };
    return stack->nodes[--stack->current_size];
}


static __attribute__((always_inline)) int _push_left_branch_in_stack(rbt_array_stack_t *stack, rbt_node_t *node) {
    while(node) {
        rbt_stack_push(stack, node, false);
        node = node->left;
    }
    return 0;
}

static __attribute__((always_inline)) int loss_monitor_init(picoquic_cnx_t *cnx, loss_monitor_t *monitor,
                                                            uint64_t current_time, uint64_t estimations_granularity,
                                                            uint64_t estimations_update_interval_microsec, uint64_t event_lifetime_microsec) {
    my_memset(monitor, 0, sizeof(loss_monitor_t));
    rbt_stack_init(&monitor->recursion_helper_stack);
    rbt_init(cnx, &monitor->packet_events);
    monitor->last_estimation_update_timestamp = current_time;
    monitor->estimations_update_interval = estimations_update_interval_microsec;
    monitor->event_lifetime_microsec = event_lifetime_microsec;

    monitor->estimations.granularity = estimations_granularity;
    monitor->estimations.r_times_granularity = estimations_granularity; // start r as 1
    PROTOOP_PRINTF(cnx, "INIT MONITOR WITH R = %lu\n", monitor->estimations.r_times_granularity);
    return 0;
}

static __attribute__((always_inline)) int remove_expired_events(picoquic_cnx_t *cnx, loss_monitor_t *monitor, uint64_t current_time) {
    if (rbt_is_empty(cnx, &monitor->packet_events)) {
        // nothing to do
        return 0;
    }

    while(!rbt_is_empty(cnx, &monitor->packet_events) && (current_time - ((packet_event_t *) rbt_min_val(cnx, &monitor->packet_events))->send_time) >= monitor->event_lifetime_microsec) {
        packet_event_t *event = (packet_event_t *) rbt_min_val(cnx, &monitor->packet_events);
        my_free(cnx, event);
        rbt_delete_min(cnx, &monitor->packet_events);
    }
    return 0;
}

static __attribute__((always_inline)) int update_estimations(picoquic_cnx_t *cnx, loss_monitor_t *monitor, uint64_t granularity, uint64_t current_time) {
    PROTOOP_PRINTF(cnx, "UPDATE ESTIMATIONS\n");
    remove_expired_events(cnx, monitor, current_time);
    uint64_t n_losses = 0;
    uint64_t n_acks = 0;

#define GOOD_STATE 0
#define BAD_STATE 1

    uint64_t state_transisions[2][2] = { {0UL, 0UL}, {0UL, 0UL} };

    int current_state = GOOD_STATE;

    if (!rbt_is_empty(cnx, &monitor->packet_events)) {
        PROTOOP_PRINTF(cnx, "TREE NOT EMPTY\n");
        rbt_array_stack_t *stack = &monitor->recursion_helper_stack;
        rbt_stack_reset(stack);
        _push_left_branch_in_stack(stack, monitor->packet_events.root);
        PROTOOP_PRINTF(cnx, "STACK SIZE = %d\n", rbt_stack_size(stack));
        rbt_node_t *current_node = NULL;
        packet_event_t *current_event = NULL;
        int new_state;

        while(rbt_stack_size(stack) != 0) {
             current_node = rbt_stack_pop(stack).node;
             current_event = (packet_event_t *) current_node->val;
             PROTOOP_PRINTF(cnx, "EVENT PN = %lx: %d\n", current_event->packet_number, current_event->event_type);
             if (current_event->event_type == loss_monitor_packet_event_acknowledged) {
                 new_state = GOOD_STATE;
                 n_acks++;
             } else {
                 new_state = BAD_STATE;
                 n_losses++;
             }
             state_transisions[current_state][new_state]++;
             current_state = new_state;
             // push the left branch of the right child
            _push_left_branch_in_stack(stack, current_node->right);
        }

    }


    PROTOOP_PRINTF(cnx, "N ACKS = %lu, N LOSSES = %lu\n", n_acks, n_losses);

    monitor->estimations.uniform_rate_times_granularity = monitor->estimations.p_times_granularity = 0;
    monitor->estimations.r_times_granularity = granularity;

    if (n_losses + n_acks > 0) {
        monitor->estimations.uniform_rate_times_granularity = granularity*n_losses/(n_losses+n_acks);
    }

    uint64_t total_good_state = (state_transisions[GOOD_STATE][BAD_STATE] + state_transisions[GOOD_STATE][GOOD_STATE]);
    uint64_t total_bad_state = (state_transisions[BAD_STATE][BAD_STATE] + state_transisions[BAD_STATE][GOOD_STATE]);
    if (total_good_state > 0) {
        monitor->estimations.p_times_granularity = granularity*state_transisions[GOOD_STATE][BAD_STATE]/total_good_state;
    }
    if (total_bad_state > 0) {
        monitor->estimations.r_times_granularity = granularity*state_transisions[BAD_STATE][GOOD_STATE]/total_bad_state;
    }

    monitor->last_estimation_update_timestamp = current_time;

    monitor->has_done_estimations = true;
    return 0;
}


static __attribute__((always_inline)) int handle_packet_event(picoquic_cnx_t *cnx, loss_monitor_t *monitor, int64_t pn,
                                                                uint64_t send_time, loss_monitor_packet_event_type_t event_type) {
    packet_event_t *event = my_malloc(cnx, sizeof(packet_event_t));
    if (!event) {
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(event, 0, sizeof(packet_event_t));

    event->packet_number = pn;
    event->send_time = send_time;
    event->event_type = event_type;

    // ensure the max size is respected
    if (rbt_size(cnx, &monitor->packet_events) == MAX_STORED_EVENTS) {
        packet_event_t *event = (packet_event_t *) rbt_min_val(cnx, &monitor->packet_events);
        my_free(cnx, event);
        rbt_delete_min(cnx, &monitor->packet_events);
    }

    rbt_put(cnx, &monitor->packet_events, pn, event);
    return 0;
}


static __attribute__((always_inline)) int loss_monitor_packet_acknowledged(picoquic_cnx_t *cnx, loss_monitor_t *monitor, int64_t pn, uint64_t send_time) {
    return handle_packet_event(cnx, monitor, pn, send_time, loss_monitor_packet_event_acknowledged);
}

static __attribute__((always_inline)) int loss_monitor_packet_lost(picoquic_cnx_t *cnx, loss_monitor_t *monitor, int64_t pn, uint64_t send_time) {
    return handle_packet_event(cnx, monitor, pn, send_time, loss_monitor_packet_event_lost);
}

#endif //PICOQUIC_LOSS_MONITOR_H
