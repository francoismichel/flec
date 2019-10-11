#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"
#include "../framework_receiver.h"

// we here assume a single-path context

protoop_arg_t process_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    window_repair_frame_t *rf = (window_repair_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (!rf)
        return PICOQUIC_ERROR_MEMORY;
    // if we reach here, we know that rf-<symbols has been instantiated
    window_receive_repair_symbols(cnx, (window_fec_framework_receiver_t *) state->framework_receiver, rf->symbols, rf->n_repair_symbols);
    // we don't delete nothing, the symbols are used by the framework and the rf itself will be freed by the core...
    return (protoop_arg_t) 0;
}