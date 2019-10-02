#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"

// we here assume a single-path context

protoop_arg_t post_write_recovered_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    PROTOOP_PRINTF(cnx, "HAS WRITTEN A RECOVERED FRAME !!\n");
    state->has_written_recovered_frame = true;
    return 0;
}