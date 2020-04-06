#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec.h"
#include "../../window_framework/framework_receiver.h"
#include "../types.h"

// we here assume a single-path context

protoop_arg_t notify_window_rwin_frame(picoquic_cnx_t *cnx)
{
    reserve_frame_slot_t *rfs = (reserve_frame_slot_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    /* Commented out, can be used if needed */
    /* int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1); */



    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_rwin_frame_t *rwin_frame = (window_rwin_frame_t *) rfs->frame_ctx;
    if (received && !state->current_packet_is_lost) {
        wff->last_acknowledged_smallest_considered_id = MAX(wff->last_acknowledged_smallest_considered_id, rwin_frame->smallest_id);
    } else {
        // to trigger at least one retransmission
        wff->a_window_frame_has_been_lost = true;
    }
    delete_window_rwin_frame(cnx, rwin_frame);
    my_free(cnx, rfs);

    return 0;
}