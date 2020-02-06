#ifndef buffer_limited_cauSAL_H
#define buffer_limited_cauSAL_H

#include <picoquic.h>
#include <getset.h>
#include "../helpers.h"

#define SOONEST_EVENT_UNDEFINED 0

typedef struct {
    uint64_t soonest_event;
} set_next_wake_time_event_state_t;




static __attribute__((always_inline)) set_next_wake_time_event_state_t *get_set_next_wake_time_event_state(picoquic_cnx_t *cnx) {

    set_next_wake_time_event_state_t *state = (set_next_wake_time_event_state_t *) get_cnx_metadata(cnx, 0);

    if (!state) {
        PROTOOP_PRINTF(cnx, "INIT NEW NO PACING STATE\n");
        state = my_malloc(cnx, sizeof(set_next_wake_time_event_state_t));
        if (!state) {
            PROTOOP_PRINTF(cnx, "ERROR: COULD NOT ALLOCATE NO PACING STATE\n");
            return NULL;
        }
        my_memset(state, 0, sizeof(set_next_wake_time_event_state_t));
        state->soonest_event = SOONEST_EVENT_UNDEFINED;
        set_cnx_metadata(cnx, 0, (protoop_arg_t) state);
    }
    return state;

}


#endif // buffer_limited_cauSAL_H