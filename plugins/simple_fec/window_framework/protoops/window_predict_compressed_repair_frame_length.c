#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "compressed_repair_frame.h"

// we here assume a single-path context

protoop_arg_t window_predict_repair_frame_length(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        // there is no mean to alert an error...
        return PICOQUIC_ERROR_MEMORY;

    window_repair_frame_t *rf = (window_repair_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    size_t retval = predict_compressed_window_repair_frame(cnx, rf, state->symbol_size);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) retval);
    return (protoop_arg_t) 0;
}