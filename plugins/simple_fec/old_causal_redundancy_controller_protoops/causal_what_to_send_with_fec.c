#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"
#include "causal_redundancy_controller_new.h"
#include "../window_framework/framework_sender.h"

// we here assume a single-path context
static __attribute__((always_inline)) int get_current_window_bounds_helper(picoquic_cnx_t *cnx, window_source_symbol_id_t *start, window_source_symbol_id_t *end) {
    protoop_arg_t out[2];
    int err = (int) run_noparam(cnx, PROTOOP_GET_CURRENT_WINDOW_BOUNDS, 0, NULL, out);
    *start = out[0];
    *end = out[1];
    return err;
}

protoop_arg_t causal_what_to_send(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    available_slot_reason_t reason = (available_slot_reason_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    if (wff->next_message_timestamp_microsec != UNDEFINED_SYMBOL_DEADLINE && wff->next_message_timestamp_microsec <= picoquic_current_time()) {
        // reset the next message timestamp if it has passe
        wff->next_message_timestamp_microsec = UNDEFINED_SYMBOL_DEADLINE;
    }
    fec_window_t window = get_current_fec_window(cnx, wff);
    if (window_size(&window) > 0) {
        run_algo(cnx, path, (causal_redundancy_controller_t *) wff->controller, reason, &window);
    }
    window_source_symbol_id_t first_id_to_protect;
    uint16_t number_of_symbols_to_protect;
    causal_packet_type_t ptype = what_to_send(cnx, wff->controller, &first_id_to_protect, &number_of_symbols_to_protect);
    number_of_symbols_to_protect = MIN(number_of_symbols_to_protect, window_size(&window));
    what_to_send_t wts;
    switch(ptype) {
        case fec_packet:
            wts = what_to_send_repair_symbol;
            break;
        case fb_fec_packet:
            wts = what_to_send_feedback_implied_repair_symbol;
            break;
        default:
            wts = what_to_send_new_symbol;
            break;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, wts);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, first_id_to_protect);
    set_cnx(cnx, AK_CNX_OUTPUT, 2, number_of_symbols_to_protect);
    return 0;
}