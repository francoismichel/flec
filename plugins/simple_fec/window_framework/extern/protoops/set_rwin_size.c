
#include <picoquic.h>
#include <getset.h>
#include "../../../fec.h"
#include "../../framework_receiver.h"

/**
 * MUST BE CALLED BEFORE REPAIR SYMBOLS ARE ACTUALLY RECEIVED !!
 */
protoop_arg_t set_rwin_size(picoquic_cnx_t *cnx)
{
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    size_t buffer_size = (size_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    size_t repair_buffer_size = (size_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    int margin = 32;    // to be sure we can store metadata etc.
    window_set_buffers_sizes(cnx, buffer_size/(state->symbol_size + margin), repair_buffer_size/(state->symbol_size + margin));
    return 0;
}