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

    // we are forced to malloc something because it will be freed by the core in skip_frame...
    window_source_symbol_id_t *id = my_malloc(cnx, sizeof(window_source_symbol_id_t));
    if (!id)
        return PICOQUIC_ERROR_MEMORY;
    *id = 0;
    uint8_t* bytes_protected = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    PROTOOP_PRINTF(cnx, "PARSE SRC FPI, IN SKIP FRAME = %d\n", state->is_in_skip_frame);
    // type byte
    bytes_protected++;

    size_t consumed = 0;
    int err = decode_window_source_symbol_id(bytes_protected, bytes_max - bytes_protected, id, &consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) id);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, false);
    set_cnx(cnx, AK_CNX_OUTPUT, 2, false);
    return (protoop_arg_t) bytes_protected + consumed;
}