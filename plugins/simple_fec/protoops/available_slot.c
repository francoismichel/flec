#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"

// we here assume a single-path context


static __attribute__((always_inline)) int reserve_fpi_frame(picoquic_cnx_t *cnx, plugin_state_t *state) {
    int err = 0;
    source_symbol_id_t id;
    get_next_source_symbol_id(cnx, state->framework_sender, &id);
    err = reserve_src_fpi_frame(cnx, id);
    if (!err)
        state->n_reserved_id_or_repair_frames++;
    return err;
}


protoop_arg_t available_slot(picoquic_cnx_t *cnx) {
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    available_slot_reason_t reason = (available_slot_reason_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    state->has_fec_protected_data_to_send = fec_has_protected_data_to_send(cnx);
    protoop_arg_t args[1];
    args[0] = reason;
    what_to_send_t wts = 0;
    source_symbol_id_t first_id = 0;
    uint16_t n_symbols_to_protect;
    int err = fec_what_to_send(cnx, path, current_time, reason, &wts, &first_id, &n_symbols_to_protect);
    if (err) {
        PROTOOP_PRINTF(cnx, "WHAT TO SEND ERROR: %d\n", err);
        return err;
    }
    PROTOOP_PRINTF(cnx, "WTS = %d!\n", wts);

    if (state->n_reserved_id_or_repair_frames > 0 && wts != what_to_send_feedback_implied_repair_symbol) {
        fec_cancelled_packet(cnx, wts, first_id, n_symbols_to_protect);
        PROTOOP_PRINTF(cnx, "ALREADY RESERVED FRAMES ARE PRESENT\n");
        return 0;
    }

    switch (wts) {
        case what_to_send_new_symbol:
            if (state->has_fec_protected_data_to_send) {
                err = reserve_fpi_frame(cnx, state);
                break;
            }
            // do not flush, let the algo do the flushing
            break;
            // otherwise, we fallthrough and send a repair symbol
        case what_to_send_feedback_implied_repair_symbol:
        case what_to_send_repair_symbol: {
            protoop_arg_t could_reserve = 0;
            PROTOOP_PRINTF(cnx, "RESERVE, FIRST ID = %u\n", first_id);
            err = reserve_repair_frames(cnx, state->framework_sender, DEFAULT_SLOT_SIZE, state->symbol_size,
                                        wts == what_to_send_feedback_implied_repair_symbol, wts == what_to_send_feedback_implied_repair_symbol,
                                        first_id, n_symbols_to_protect, &could_reserve);
            if (!could_reserve) {
                // if we could not reserve, we try sending a new packet
                if (state->has_fec_protected_data_to_send) {
                    err = reserve_fpi_frame(cnx, state);
                }
            }
            break;
        }
    }
    return err;
}
