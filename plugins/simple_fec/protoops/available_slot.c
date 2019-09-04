#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"

// we here assume a single-path context

protoop_arg_t available_slot(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    what_to_send_t wts = (what_to_send_t) run_noparam(cnx, FEC_PROTOOP_WHAT_TO_SEND, 0, NULL, NULL);
    int err = 0;
    switch (wts) {
        case what_to_send_new_symbol:
            if (run_noparam(cnx, FEC_PROTOOP_HAS_PROTECTED_DATA_TO_SEND, 0, NULL, NULL)) {
                source_symbol_id_t id;
                get_next_source_symbol_id(cnx, state->framework_sender, &id);
                err = reserve_src_fpi_frame(cnx, id);
                break;
            } // otherwise, we fallthrough and send a repair symbol
        case what_to_send_repair_symbol:
            break;
    }
    state->has_written_fpi_frame = false;
    return err;
}