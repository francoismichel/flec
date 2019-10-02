#include "picoquic.h"
#include "../fec_protoops.h"


/**
 * cnx->protoop_inputv[0] = fec_frame_t* frame
 *
 * Output: uint8_t* bytes
 */
protoop_arg_t skip_frame(picoquic_cnx_t *cnx)
{
    picoquic_state_enum cnx_state = (picoquic_state_enum) get_cnx(cnx, AK_CNX_STATE, 0);
    if (cnx_state == picoquic_state_client_ready || cnx_state == picoquic_state_server_ready) {
        bpf_state *state = get_bpf_state(cnx);
        if (!state)
            return PICOQUIC_ERROR_MEMORY;
        state->is_in_skip_frame = false;
    }
    return (protoop_arg_t) 0;
}