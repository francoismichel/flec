#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t post_write_repair_frame(picoquic_cnx_t *cnx) {
    protoop_arg_t retval = get_cnx(cnx, AK_CNX_RETURN_VALUE, 0);
    // has_written_(fb_fec_?)repair_frame is done in write_frame directly



    PROTOOP_PRINTF(cnx, "POST WRITE REPAIR\n");
    if (retval != PICOQUIC_MISCCODE_RETRY_NXT_PKT) {
        size_t consumed = get_cnx(cnx, AK_CNX_OUTPUT, 0);
        bool retransmittable = get_cnx(cnx, AK_CNX_OUTPUT, 1);
        plugin_state_t *state = get_plugin_state(cnx);
        if (!state)
            return PICOQUIC_ERROR_MEMORY;
        state->n_reserved_id_or_repair_frames--;
        if (consumed  > 0) { // if bytes have been written
            state->n_repair_frames_sent_since_last_feedback++;
        }
        set_cnx(cnx, AK_CNX_OUTPUT, 0, consumed);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, retransmittable);
    }
    return 0;
}