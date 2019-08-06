#include "../../helpers.h"
#include "../fec.h"
#include "../fec_protoops.h"


/**
 * See PROTOOP_NOPARAM_STREAM_BYTES_MAX
 */

protoop_arg_t stream_bytes_max(picoquic_cnx_t* cnx) {
    size_t bytes_max = get_cnx(cnx, AK_CNX_INPUT, 0);
    // FIXME there is one more byte than needed (?) in the overhead, but without it, it doesn't work. Find out why
//    size_t overhead = 1 + (1 + sizeof(uint64_t)) + (DEFAULT_K/2 + sizeof(fec_frame_header_t));
    bpf_state *state = get_bpf_state(cnx);
    if (!state)
        return -1;
    if (state->current_packet_contains_fec_frame)
        // avoid having a stream frame with a repair frame
        bytes_max = 0;
    else {
        size_t overhead = 1 + (1 + sizeof(uint64_t)) + sizeof(fec_frame_header_t);
        bytes_max = (bytes_max && bytes_max > overhead) ? bytes_max-overhead : bytes_max;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, bytes_max);
    return 0;
}