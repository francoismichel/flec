#include <picoquic.h>
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

/**
 * See PROTOOP_NOPARAM_FINALIZE_AND_PROTECT_PACKET
 */
protoop_arg_t window_packet_sent(picoquic_cnx_t *cnx) {
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 9);
    fec_check_for_available_slot(cnx, available_slot_reason_none, current_time);
    return 0;
}