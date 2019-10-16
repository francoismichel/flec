#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../tetrys_framework.h"
#include "../tetrys_framework_sender.c"

// we here assume a single-path context

protoop_arg_t reserve_repair_frames_protoop(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    tetrys_fec_framework_sender_t *ff = (tetrys_fec_framework_sender_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint16_t max_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    bool feedback_implied = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 3);
    bool protect_subset = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    tetrys_source_symbol_id_t first_id_to_protect = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 5);
    uint16_t n_symbols_to_protect = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 6);
    int err = 0;

    picoquic_state_enum cnx_state = get_cnx(cnx, AK_CNX_STATE, 0);
    // TODO: remove cnx_state part of else if

    if (ff->common_fec_framework.buffered_repair_symbols.size > 0) {
        err = tetrys_reserve_repair_frames(cnx, ff, max_size, symbol_size, feedback_implied);
        if (!err)
            state->n_reserved_id_or_repair_frames++;
    } else if (feedback_implied || (cnx_state == picoquic_state_server_ready/* && state->n_repair_frames_sent_since_last_feedback <= max_repair_frames_threshold *//*wff->window_length*2*/)) {

        PROTOOP_PRINTF(cnx, "NOTHING TO RESERVE, GENERATE !\n");
        err = flush_tetrys(cnx, ff, state->symbol_size);
        if (buffer_peek_symbol_payload_size(cnx, &ff->common_fec_framework.buffered_repair_symbols, max_size)) {

//        err = generate_and_queue_repair_symbols(cnx, wff, true, PICOQUIC_MAX_PACKET_SIZE/state->symbol_size /*+ 1*/, state->symbol_size, protect_subset, first_id_to_protect, n_symbols_to_protect);
            if (!err)
                err = tetrys_reserve_repair_frames(cnx, ff, max_size, symbol_size, feedback_implied);
//            err = window_reserve_repair_frames(cnx, wff, max_size, symbol_size, feedback_implied);
            if (!err)
                state->n_reserved_id_or_repair_frames++;    // TODO: not ++ but + something
        }
    }
    return err;
}