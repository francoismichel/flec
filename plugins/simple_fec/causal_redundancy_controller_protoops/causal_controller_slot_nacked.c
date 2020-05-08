
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "causal_redundancy_controller.h"
#include "../window_framework/framework_sender.h"

protoop_arg_t causal_slot_nacked(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        return PICOQUIC_ERROR_MEMORY;
    }
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    window_redundancy_controller_t controller = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t slot = (source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    fec_window_t window = get_current_fec_window(cnx, wff);
    slot_nacked(cnx, controller, &window, slot);
    return 0;
}