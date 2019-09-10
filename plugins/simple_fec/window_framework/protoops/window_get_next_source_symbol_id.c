#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t get_next_ssid(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_t *wff = (window_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, window_get_next_source_symbol_id(wff));
    return 0;
}