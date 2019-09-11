#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"
#include "../framework_receiver.h"

protoop_arg_t fec_get_current_window_bounds(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    window_source_symbol_id_t start, end;
    get_current_window_bounds(cnx, (window_fec_framework_t *) state->framework_sender, &start, &end);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, start);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, end);
    return 0;
}