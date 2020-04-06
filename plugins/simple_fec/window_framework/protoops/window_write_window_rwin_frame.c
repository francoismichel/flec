#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_receiver.h"

// we here assume a single-path context

protoop_arg_t write_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_rwin_frame_t *frame = (window_rwin_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 2);
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;

    frame->smallest_id = wff->smallest_considered_id_to_advertise;
    frame->window_size = wff->receive_buffer_size;

    my_memset(bytes, FRAME_WINDOW_RWIN, 1);
    bytes++;

    size_t consumed = 0;
    int err = serialize_window_rwin_frame(bytes, bytes_max - bytes, frame, &consumed);
    if (!err) {
        wff->a_window_frame_has_been_written = true;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 1 + consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 1);
    return err;
}