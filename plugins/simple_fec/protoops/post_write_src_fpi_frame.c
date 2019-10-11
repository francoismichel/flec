#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t post_write_src_fpi_frame(picoquic_cnx_t *cnx) {
    protoop_arg_t retval = get_cnx(cnx, AK_CNX_RETURN_VALUE, 0);
    size_t consumed = get_cnx(cnx, AK_CNX_OUTPUT, 0);
    bool retransmittable = get_cnx(cnx, AK_CNX_OUTPUT, 1);
    source_symbol_id_t current_id = (source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    if (retval != PICOQUIC_MISCCODE_RETRY_NXT_PKT) {
        state->n_reserved_id_or_repair_frames--;
        if (consumed  > 0) { // if bytes have been written
            state->has_written_fpi_frame = true;
            state->current_id = current_id;
        }
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, retransmittable);
    return 0;
}