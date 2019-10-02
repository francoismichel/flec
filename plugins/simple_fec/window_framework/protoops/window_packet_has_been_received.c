
#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../utils.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t packet_has_been_received(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    uint64_t packet_number = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t packet_slot = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_source_symbol_id_t first_source_symbol_id = (source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    uint16_t n_source_symbols_in_packet = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 3);
    window_slot_acked(cnx, (window_fec_framework_t *) state->framework_sender, packet_slot);
    PROTOOP_PRINTF(cnx, "PACKET %lx HAS BEEN RECEIVED, CONTAINS %u SYMBOLS !\n", packet_number, n_source_symbols_in_packet);
    for (window_source_symbol_id_t id = first_source_symbol_id ; id < first_source_symbol_id + n_source_symbols_in_packet ; id++) {
        sfpid_has_landed(cnx, (window_fec_framework_t *) state->framework_sender, id, true);
    }
    // no slot available
    return 0;
}