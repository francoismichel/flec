#include "picoquic.h"
#include "plugin.h"
#include "../helpers.h"
#include "no_pacing_with_event.h"

/**
 * See PROTOOP_NOPARAM_SET_NEXT_WAKE_TIME
 */
protoop_arg_t request_waking_at_last_at(picoquic_cnx_t *cnx)
{
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t min_timestamp = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);

    set_next_wake_time_event_state_t *state = get_set_next_wake_time_event_state(cnx);
    if (!state) {
        PROTOOP_PRINTF(cnx, "ERROR: COULD NOT GET THE STATE\n");
        return PICOQUIC_ERROR_MEMORY;
    }

    if(state->soonest_event == SOONEST_EVENT_UNDEFINED || state->soonest_event <= current_time || min_timestamp < state->soonest_event) {
        state->soonest_event = min_timestamp;
        PROTOOP_PRINTF(cnx, "WAKE AT LAST AT %lu\n", state->soonest_event);
    }

    return 0;
}