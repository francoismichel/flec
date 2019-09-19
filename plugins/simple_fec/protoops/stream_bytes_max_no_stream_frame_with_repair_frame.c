#include "../../helpers.h"
#include "../fec.h"


/**
 * See PROTOOP_NOPARAM_STREAM_BYTES_MAX
 */

protoop_arg_t stream_bytes_max(picoquic_cnx_t* cnx) {
    size_t bytes_max = get_cnx(cnx, AK_CNX_INPUT, 0);
    // FIXME there is one more byte than needed (?) in the overhead, but without it, it doesn't work. Find out why
//    size_t overhead = 1 + (1 + sizeof(uint64_t)) + (DEFAULT_K/2 + sizeof(fec_frame_header_t));
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return -1;
    if (state->has_written_repair_frame || state->has_written_fb_fec_repair_frame)
        // avoid having a stream frame with a repair frame
        bytes_max = 0;
    else {
        // TODO: check if needed
//        size_t overhead = 1 + (1 + sizeof(uint64_t)) + sizeof(fec_frame_header_t);
//        bytes_max = (bytes_max && bytes_max > overhead) ? bytes_max-overhead : bytes_max;
        size_t overhead = 50;
        bytes_max = (bytes_max && bytes_max > SYMBOL_SIZE - overhead) ? SYMBOL_SIZE-overhead : bytes_max;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, bytes_max);
    return 0;
}