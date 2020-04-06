

#include <picoquic.h>
#include <getset.h>

#include "../../fec.h"
#include "../framework_sender.h"

protoop_arg_t connection_state_changed(picoquic_cnx_t *cnx)
{
    // We ensure to laod the FEC frameworks and schemes as soon as possible in the connection life
    picoquic_state_enum from_state = (picoquic_state_enum) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (from_state == picoquic_state_client_init || from_state == picoquic_state_server_almost_ready) {
        plugin_state_t *state = get_plugin_state(cnx);
        if (!state) {
            return PICOQUIC_ERROR_MEMORY;
        }
        window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
        // allows to send at least 10 symbols if permitted
        if (wff->window_control.largest_authorized_id_by_peer == 0) {
            // TODO: use real transport parameters for FEC
            size_t initial_max_data = get_cnx(cnx, AK_CNX_REMOTE_PARAMETER, TRANSPORT_PARAMETER_INITIAL_MAX_DATA);
            size_t allowed_symbols = MAX(1, initial_max_data/state->symbol_size);
            update_window_bounds(cnx, wff, 0, MIN(10, allowed_symbols - 1));
        }
    }
    return 0;
}