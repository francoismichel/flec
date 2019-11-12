
#include <picoquic.h>
#include <picoquic.h>
#include "../../../helpers.h"

/**
 * See PROTOOP_NOPARAM_STREAM_BYTES_MAX
 */

protoop_arg_t stream_bytes_max(picoquic_cnx_t* cnx) {
    size_t bytes_max = get_cnx(cnx, AK_CNX_INPUT, 0);
    size_t overhead = 1 + (1 + sizeof(uint64_t)) + 10/*sizeof(fec_frame_header_t)*/ + 20;
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (bytes_max && bytes_max > overhead) ? bytes_max-overhead : (bytes_max-overhead));
    return 0;
}