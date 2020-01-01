#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"
#include "../framework_receiver.h"

// we here assume a single-path context

protoop_arg_t protect_packet_payload_protoop(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    uint8_t *payload = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    size_t payload_length = (size_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t packet_number = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    source_symbol_id_t first_symbol_id = 0;
    uint16_t n_symbols = 0;

    symbol_deadline_t deadline = ((window_fec_framework_t *) state->framework_sender)->min_deadline_in_current_packet;
    int err = window_protect_packet_payload(cnx, (window_fec_framework_t *) state->framework_sender, payload, payload_length,
            packet_number, &first_symbol_id, &n_symbols, state->symbol_size, deadline);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, first_symbol_id);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, n_symbols);
    return err;
}