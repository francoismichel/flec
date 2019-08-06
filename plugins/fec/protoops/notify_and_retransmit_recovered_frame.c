#include "../../helpers.h"
#include "../fec_protoops.h"
#include "../framework/window_framework_sender.h"


protoop_arg_t notify_recovered_frame(picoquic_cnx_t *cnx)
{
    reserve_frame_slot_t *rfs = (reserve_frame_slot_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    /* Commented out, can be used if needed */
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;
    int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1);
    int retval = 0;
    recovered_packets_t *rp = (recovered_packets_t *) rfs->frame_ctx;
    if (!received || state->current_packet_is_lost) {
        // try to resend the frame: re-reserve the rfs
        size_t reserved_size = reserve_frames(cnx, 1, rfs);
        PROTOOP_PRINTF(cnx, "RESERVED FOR FRAME %u (%lu bytes)\n", rfs->frame_type, reserved_size);
        if (reserved_size < rfs->nb_bytes) {
            PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
            free_rp(cnx, rp);
            my_free(cnx, rfs);
            retval = -1;
        }
    } else {
        free_rp(cnx, rp);
        my_free(cnx, rfs);
    }
    // the frame_ctx must be freed in the write_frame

    return retval;
}