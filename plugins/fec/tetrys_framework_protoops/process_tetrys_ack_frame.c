#include "picoquic.h"
#include "../fec_protoops.h"
#include "../framework/tetrys_framework.h"


/**
 * cnx->protoop_inputv[0] = fec_frame_t* frame
 *
 * Output: uint8_t* bytes
 */
protoop_arg_t process_tetrys_ack_frame(picoquic_cnx_t *cnx)
{
    tetrys_ack_frame_t *frame = (tetrys_ack_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    PROTOOP_PRINTF(cnx, "PROCESS TETRYS ACK\n");
    bpf_state *state = get_bpf_state(cnx);
    int err = 0;
    if (!state || !tetrys_receive_ack(cnx, state->framework_sender, frame)) {
        err = -1;
    }
    my_free(cnx, frame->data);
    return (protoop_arg_t) err;
}