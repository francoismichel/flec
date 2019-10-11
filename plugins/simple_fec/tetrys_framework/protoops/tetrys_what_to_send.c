#include <picoquic.h>
#include <getset.h>
#include "../../fec.h"
#include "../tetrys_framework.h"

protoop_arg_t causal_what_to_send(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    tetrys_fec_framework_sender_t *ff = (tetrys_fec_framework_sender_t *) state->framework_sender;
    what_to_send_t wts;
    if (ff->common_fec_framework.buffered_repair_symbols.size > 0) {
        // currently, we ignore feedback-implied repair symbols
        wts = what_to_send_repair_symbol;
    } else {
        wts = what_to_send_new_symbol;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, wts);
    // these two ones are only needed by the framework, not the core plugin, so it can be put to 0
    set_cnx(cnx, AK_CNX_OUTPUT, 1, 0);
    set_cnx(cnx, AK_CNX_OUTPUT, 2, 0);
    return 0;
}