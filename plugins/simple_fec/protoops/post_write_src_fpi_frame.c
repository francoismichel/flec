#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t post_write_src_fpi_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    state->has_written_fpi_frame = true;
    state->n_reserved_id_or_repair_frames--;
    state->current_id = (source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    return 0;
}