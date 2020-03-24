#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t process_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    window_rwin_frame_t *frame = (window_rwin_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (!frame) {
        return PICOQUIC_ERROR_MEMORY;
    }
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    PROTOOP_PRINTF(cnx, "PROCESS RWIN FRAME first %lu size %lu\n", frame->smallest_id, frame->window_size);
    return update_window_bounds(cnx, wff, frame->smallest_id, frame->smallest_id + frame->window_size - 1);
    // we don't need to free because the core will do it...
//    my_free(cnx, id);
}