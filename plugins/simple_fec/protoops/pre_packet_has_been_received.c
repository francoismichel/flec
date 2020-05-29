#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t pre_packet_has_been_received(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return -1;
    uint64_t received_packet_number = get_cnx(cnx, AK_CNX_INPUT, 0);
//    uint64_t slot = get_cnx(cnx, AK_CNX_INPUT, 1);
    bool fec_protected = get_cnx(cnx, AK_CNX_INPUT, 4);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 7);
    if (fec_protected) {
        if (DEBUG_EVENT) {
            uint64_t send_time = get_cnx(cnx, AK_CNX_INPUT, 6);
            PROTOOP_PRINTF(cnx, "EVENT::{\"time\": %ld, \"type\": \"packet_received_by_peer\", \"pn\": %ld, \"elapsed\": %ld}\n", picoquic_current_time(), received_packet_number, picoquic_current_time() - send_time);

            PROTOOP_PRINTF(cnx, "[[PACKET %lx RECEIVED IN %lu]]\n", received_packet_number, picoquic_current_time() - send_time);
        }
    }
    int err = 0;
    if ((err = fec_check_for_available_slot(cnx, available_slot_reason_ack, current_time)) != 0)
        return err;
    return err;
}