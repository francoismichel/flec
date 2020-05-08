#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context
// in output[0], we place a boolean telling the caller if a frame could be reserved
protoop_arg_t reserve_repair_frames_protoop(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_t *wff = (window_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint16_t max_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    bool feedback_implied = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 3);
    bool protect_subset = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    window_source_symbol_id_t first_id_to_protect = (window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 5);
    uint16_t n_symbols_to_protect = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 6);
    int max_repair_frames_threshold = 50;
//    int max_repair_frames_threshold = 10;
//    if (!is_lost_packet_queue_empty(cnx, &state->lost_packets))
//        max_repair_frames_threshold = wff->window_length*2;
    int err = 0;

    picoquic_state_enum cnx_state = get_cnx(cnx, AK_CNX_STATE, 0);

    bool has_reserved = false;

    // TODO: remove cnx_state part of else if

    if (has_frames_to_reserve(cnx, wff)) {
        err = window_reserve_repair_frames(cnx, wff, max_size, symbol_size, feedback_implied);
        if (!err)
            state->n_reserved_id_or_repair_frames++;
    } else if (feedback_implied || (cnx_state == picoquic_state_server_ready && state->n_repair_frames_sent_since_last_feedback <= max_repair_frames_threshold/*wff->window_length*2*/)) {

        PROTOOP_PRINTF(cnx, "NOTHING TO RESERVE, GENERATE !\n");
        if (!is_fec_window_empty(wff)) {
            PROTOOP_PRINTF(cnx, "GENERATE %d REPAIR SYMBOLS, FEEDBACK-IMPLIED = %d, PROTECT SUBSET = %d, FIRST = %u, n_symbols = %u\n", PICOQUIC_MAX_PACKET_SIZE/state->symbol_size/* + 1*/, feedback_implied, protect_subset, first_id_to_protect, n_symbols_to_protect);
            err = generate_and_queue_repair_symbols(cnx, wff, true, PICOQUIC_MAX_PACKET_SIZE/state->symbol_size /*+ 1*/, state->symbol_size, protect_subset, first_id_to_protect, n_symbols_to_protect);
            if (!err)
                err = window_reserve_repair_frames(cnx, wff, max_size, symbol_size, feedback_implied);
            if (!err) {
                state->n_reserved_id_or_repair_frames++;    // TODO: not ++ but + something
                has_reserved = true;
                if (!feedback_implied)
                    state->n_repair_frames_sent_since_last_feedback++;
            }
        } else {
            PROTOOP_PRINTF(cnx, "EMPTY WINDOW, CANNOT GENERATE\n");
        }
    } else {
        PROTOOP_PRINTF(cnx, "ALREADY SENT %d REPAIR FRAMES, STOP SENDING IT BEFORE NEXT FEEDBACK\n", state->n_repair_frames_sent_since_last_feedback);
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, has_reserved);
    return err;
}