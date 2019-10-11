#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec.h"
#include "../wire.h"

// we here assume a single-path context

protoop_arg_t notify_recovered_frame(picoquic_cnx_t *cnx)
{
    reserve_frame_slot_t *rfs = (reserve_frame_slot_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    /* Commented out, can be used if needed */
    /* int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1); */



    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;
    PROTOOP_PRINTF(cnx, "NOTIFY RECOVERED FRAME !! CURRENT PACKET IS LOST = %d\n", state->current_packet_is_lost);
    int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1);
    int retval = 0;
    tetrys_recovered_frame_t *rf = (tetrys_recovered_frame_t *) rfs->frame_ctx;
    if (!received || state->current_packet_is_lost) {
        // try to resend the frame: re-reserve the rfs
        size_t reserved_size = reserve_frames(cnx, 1, rfs);
        if (reserved_size < rfs->nb_bytes) {
            PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
            delete_recovered_frame(cnx, rf);
            retval = -1;
            my_free(cnx, rfs);
        }
    } else {
        delete_recovered_frame(cnx, rf);
        my_free(cnx, rfs);
    }



    return retval;
}