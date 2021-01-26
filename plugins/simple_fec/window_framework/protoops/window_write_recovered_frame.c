#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"
#include "../framework_receiver.h"

protoop_arg_t write_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_recovered_frame_t *rf = (window_recovered_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 2);
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    if (!wff) {
        return PICOQUIC_ERROR_MEMORY;
    }
    // get a fresh recovered frame
    int err = fill_recovered_frame(cnx, wff, rf, 1000);
    if (err) {
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(bytes, FRAME_RECOVERED, 1);
    bytes++;

    size_t consumed = 0;
    err = serialize_window_recovered_frame(cnx, bytes, bytes_max - bytes, rf, &consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 1 + consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
    if (!err) {
        state->n_recovered_frames_in_flight++;
        state->n_reserved_recovered_frames--;
    }
    PROTOOP_PRINTF(cnx, "WRITTEN RF, RET = %d, N RF I FLIGHT = %ld\n", err, state->n_recovered_frames_in_flight);
    return err;
}