#include "../helpers.h"


/**
 * See PROTOOP_NOPARAM_STREAM_BYTES_MAX
 */

protoop_arg_t stream_bytes_max(picoquic_cnx_t* cnx) {
    size_t bytes_max = get_cnx(cnx, AK_CNX_INPUT, 0);
    // TODO: check if needed
    // TODO: handle a multipath scenario

//        size_t overhead = 1 + (1 + sizeof(uint64_t)) + sizeof(fec_frame_header_t);
//        bytes_max = (bytes_max && bytes_max > overhead) ? bytes_max-overhead : bytes_max;
    size_t overhead = 40;// max repair frame size
    uint64_t mtu = get_path((picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0), AK_PATH_SEND_MTU, 0) - 30; // 30 for the header overhead
    uint64_t max_pkt_size = MIN(1500, mtu);   // 30 is an ok value for header + checksum overheads
    PROTOOP_PRINTF(cnx, "MTU = %lu\n", mtu);
    PROTOOP_PRINTF(cnx, "MAX PKT SIZE - OVERHEAD = %lu - %lu = %lu, BYTES MAX = %lu\n", max_pkt_size, overhead, max_pkt_size - overhead, bytes_max);
    bytes_max = (bytes_max && (bytes_max - overhead) > (max_pkt_size - overhead)) ? (max_pkt_size-overhead) : (bytes_max-overhead);
    bytes_max++;  // adjustment
    PROTOOP_PRINTF(cnx, "RETURN STREAM BYTES MAX %lu\n", bytes_max);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, bytes_max);
    return 0;
}