#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t write_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    window_source_symbol_id_t id = (window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 2);

    PROTOOP_PRINTF(cnx, "WRITE SRC FPI, ID = %u\n", id);
    my_memset(bytes, FRAME_FEC_SRC_FPI, bytes_max - bytes);
    bytes++;

    size_t consumed = 0;
    int err = serialize_window_fpi_frame(bytes, bytes_max - bytes, id, &consumed, state->symbol_size);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 1 + consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
    return err;
}