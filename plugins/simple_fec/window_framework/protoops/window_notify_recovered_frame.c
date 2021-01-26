#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_receiver.h"

// we here assume a single-path context

protoop_arg_t notify_recovered_frame(picoquic_cnx_t *cnx)
{
    reserve_frame_slot_t *rfs = (reserve_frame_slot_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    /* Commented out, can be used if needed */
    /* int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1); */



    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;
    PROTOOP_PRINTF(cnx, "NOTIFY RECOVERED FRAME, N RESERVED RF = %lu\n", state->n_reserved_recovered_frames);
    state->n_recovered_frames_in_flight--;  // not in flight anymore
    int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1);
    int retval = 0;
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    window_recovered_frame_t *rf = (window_recovered_frame_t *) rfs->frame_ctx;
    if ((!received || state->current_packet_is_lost) && state->n_recovered_frames_in_flight == 0) {
        // only re-send a recovered frame when no recovered frame is currently in flight as they are cumulative
        // try to resend the frame: re-reserve the rfs
        int err = fill_recovered_frame(cnx, wff, rf, 1000);
        if (err) {
            size_t reserved_size = reserve_frames(cnx, 1, rfs);
            if (reserved_size < rfs->nb_bytes) {
                PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
                delete_recovered_frame(cnx, rf);
                retval = -1;
                my_free(cnx, rfs);
            } else {
                state->n_reserved_recovered_frames++;
            }
        } else {
            PROTOOP_PRINTF(cnx, "ERROR: COULD NOT RE-RESERVE A RECOVERED FRAME\n");
            retval = -1;
            my_free(cnx, rfs);
        }
    } else if (received) {
        for (int i = 0 ; i < rf->n_packets ; i++) {
            rbt_delete(cnx, &wff->recovered_packets, rf->packet_numbers[i]);
        }
        delete_recovered_frame(cnx, rf);
        my_free(cnx, rfs);
    } else {
        delete_recovered_frame(cnx, rf);
        my_free(cnx, rfs);
        PROTOOP_PRINTF(cnx, "LOST RECOVERED FRAME AT LEAST 1 RF IN FLIGHT: %lu\n", state->n_recovered_frames_in_flight);
    }



    return retval;
}