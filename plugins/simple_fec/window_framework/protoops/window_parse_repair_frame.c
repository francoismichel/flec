#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t parse_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        // there is no mean to alert an error...
        return PICOQUIC_ERROR_MEMORY;

    uint8_t* bytes_protected = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    // type byte
    bytes_protected += REPAIR_FRAME_TYPE_BYTE_SIZE;

    size_t consumed = 0;
    // we cannot signal an error...
    window_repair_frame_t *rf = parse_window_repair_frame(cnx, bytes_protected, bytes_max, state->symbol_size, &consumed, state->is_in_skip_frame);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) rf); // frame
    set_cnx(cnx, AK_CNX_OUTPUT, 1, true);              // ack needed
    set_cnx(cnx, AK_CNX_OUTPUT, 2, false);              // is retransmittable
    return (protoop_arg_t) bytes_protected + consumed;
}