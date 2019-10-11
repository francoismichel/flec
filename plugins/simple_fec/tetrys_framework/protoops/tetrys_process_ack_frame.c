#include "picoquic.h"
#include "../tetrys_framework.h"


/**
 * cnx->protoop_inputv[0] = fec_frame_t* frame
 *
 * Output: uint8_t* bytes
 */
protoop_arg_t process_tetrys_ack_frame(picoquic_cnx_t *cnx)
{
    tetrys_ack_frame_t *frame = (tetrys_ack_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    plugin_state_t *state = get_plugin_state(cnx);
    int err = 0;
    if (!state || !tetrys_receive_ack(cnx, &((tetrys_fec_framework_sender_t *) state->framework_sender)->common_fec_framework, frame)) {
        err = -1;
    }
    my_free(cnx, frame->data);
    return (protoop_arg_t) err;
}