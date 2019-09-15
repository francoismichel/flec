#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t reserve_repair_frames_protoop(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_t *wff = (window_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint16_t max_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    int err = 0;
    if (has_frames_to_reserve(cnx, wff)) {
        err = window_reserve_repair_frames(cnx, wff, max_size, symbol_size);
        if (!err)
            state->n_reserved_id_or_repair_frames++;
    } else {
        PROTOOP_PRINTF(cnx, "NOTHING TO RESERVE, GENERATE !\n");
        if (!is_fec_window_empty(wff)) {
            PROTOOP_PRINTF(cnx, "GENERATE %d REPAIR SYMBOLS\n", PICOQUIC_MAX_PACKET_SIZE/state->symbol_size + 1);
            generate_and_queue_repair_symbols(cnx, wff, true, PICOQUIC_MAX_PACKET_SIZE/state->symbol_size + 1, state->symbol_size);
            err = window_reserve_repair_frames(cnx, wff, max_size, symbol_size);
            if (!err)
                state->n_reserved_id_or_repair_frames++;
        } else {
            PROTOOP_PRINTF(cnx, "EMPTY WINDOW, CANNOT GENERATE\n");
        }
    }
    return err;
}