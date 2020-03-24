
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "causal_redundancy_controller.h"
#include "../window_framework/framework_sender.h"



protoop_arg_t causal_cancelled_packet(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    causal_packet_type_t ptype = (causal_packet_type_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    cancelled_packet(cnx, ((window_fec_framework_t *) state->framework_sender)->controller, ptype);
    return 0;
}