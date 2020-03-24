#include <picoquic.h>
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t window_packet_sent(picoquic_cnx_t *cnx) {
    fec_check_for_available_slot(cnx, available_slot_reason_none);
    return 0;
}