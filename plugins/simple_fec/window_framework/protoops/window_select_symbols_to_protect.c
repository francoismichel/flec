#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t window_select_symols_to_protect(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    window_source_symbol_t **symbols = (window_source_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 0);
    window_fec_framework_t *wff = (window_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    uint16_t n_symbols = 0;
    window_source_symbol_id_t first_id = 0;
    int err = select_all_inflight_source_symbols(cnx, wff, symbols, &n_symbols, &first_id);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, n_symbols);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, first_id);
    return err;
}