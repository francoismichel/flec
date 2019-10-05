#include "../../../helpers.h"
#include "../../fec.h"


/**
 * See PROTOOP_NOPARAM_STREAM_BYTES_MAX
 */

protoop_arg_t stream_bytes_max(picoquic_cnx_t* cnx) {
    size_t bytes_max = get_cnx(cnx, AK_CNX_INPUT, 0);
    // FIXME there is one more byte than needed (?) in the overhead, but without it, it doesn't work. Find out why
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return -1;
    if (state->has_written_repair_frame || state->has_written_fb_fec_repair_frame)
        // avoid having a stream frame with a repair frame
        bytes_max = 0;
    else {
        // TODO: check if needed
        // TODO: handle a multipath scenario
//        size_t overhead = 1 + (1 + sizeof(uint64_t)) + sizeof(fec_frame_header_t);
//        bytes_max = (bytes_max && bytes_max > overhead) ? bytes_max-overhead : bytes_max;
        size_t overhead = 25;
        uint64_t mtu = get_path((picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0), AK_PATH_SEND_MTU, 0) - 30; // max repair frame size
        uint64_t max_pkt_size = MIN(CHUNK_SIZE, mtu);   // 30 is an ok value for header + checksum overheads
        PROTOOP_PRINTF(cnx, "MTU = %lu\n", mtu);
        PROTOOP_PRINTF(cnx, "MAX PKT SIZE - OVERHEAD = %lu - %lu = %lu, BYTES MAX = %lu\n", max_pkt_size, overhead, max_pkt_size - overhead, bytes_max);
        bytes_max = (bytes_max && bytes_max > max_pkt_size - overhead) ? (max_pkt_size-overhead) : bytes_max;
        PROTOOP_PRINTF(cnx, "RETURN %lu\n", bytes_max);
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, bytes_max);
    return 0;
}