#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t pre_packet_has_been_lost(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return -1;
    // the packet is always fec-protected
    uint64_t lost_packet_number = get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t slot = get_cnx(cnx, AK_CNX_INPUT, 1);
    source_symbol_id_t first_symbol_id = get_cnx(cnx, AK_CNX_INPUT, 2);
    uint16_t n_symbols = get_cnx(cnx, AK_CNX_INPUT, 0);
    int err = 0;
    if ((err = add_lost_packet(cnx, &state->lost_packets, lost_packet_number, slot, first_symbol_id, n_symbols)) != 0)
        return err;
    if ((err = fec_check_for_available_slot(cnx, available_slot_reason_nack)) != 0)
        return err;
    return 0;
}