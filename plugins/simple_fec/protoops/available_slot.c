#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t available_slot(picoquic_cnx_t *cnx) {
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    available_slot_reason_t reason = (available_slot_reason_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    protoop_arg_t args[1];
    args[0] = reason;
    what_to_send_t wts = 0;
    int err = fec_what_to_send(cnx, reason, &wts);
    if (err)
        return err;
    switch (wts) {
        case what_to_send_new_symbol:
            if (run_noparam(cnx, FEC_PROTOOP_HAS_PROTECTED_DATA_TO_SEND, 0, NULL, NULL)) {
                source_symbol_id_t id;
                get_next_source_symbol_id(cnx, state->framework_sender, &id);
                err = reserve_src_fpi_frame(cnx, id);
                break;
            } // otherwise, we fallthrough and send a repair symbol
        case what_to_send_feedback_implied_repair_symbol:
        case what_to_send_repair_symbol:
            err = reserve_repair_frames(cnx, state->framework_sender, DEFAULT_SLOT_SIZE, state->symbol_size);
            break;
    }
    // TODO: move this before schedule_frames_on_path
    state->has_written_fpi_frame = false;
    return err;
}