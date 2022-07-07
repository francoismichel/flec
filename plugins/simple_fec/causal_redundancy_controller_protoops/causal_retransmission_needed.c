
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "causal_redundancy_controller.h"
#include "../window_framework/framework_sender.h"

protoop_arg_t causal_retransmission_needed(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        return PICOQUIC_ERROR_MEMORY;
    }
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    window_redundancy_controller_t controller = wff->controller;
    return retransmission_needed(cnx, controller) ? 1 : 0;  // ensure to convert in integer because we have had problems with conversions from bool to uint64
}