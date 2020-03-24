

#include <picoquic.h>
#include "../../fec.h"
#include "../framework_sender.h"

/**
 * See PROTOOP_NOPARAM_FIND_READY_STREAM
 *
 *
 *
 * !!!!!!!!!! must be attached to find_ready_stream and schedule_next_stream
 *
 *
 *
 */
protoop_arg_t deadline_scheduler(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        return (protoop_arg_t) NULL;
    }
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    if (!can_send_new_source_symbol(cnx, wff)) {
        PROTOOP_PRINTF(cnx, "FEC FLOW-CONTROL BLOCKED\n");
        return (protoop_arg_t) NULL;
    }
    return find_ready_stream_round_robin(cnx);
}
