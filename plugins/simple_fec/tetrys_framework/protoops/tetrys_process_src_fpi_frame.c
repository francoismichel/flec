#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../tetrys_framework.h"

// we here assume a single-path context

protoop_arg_t process_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    tetrys_source_symbol_id_t *id = (tetrys_source_symbol_id_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (!id) {
        return PICOQUIC_ERROR_MEMORY;
    }
    PROTOOP_PRINTF(cnx, "PROCESS SRC FPI FRAME, IN SKIP FRAME = %d\n", state->is_in_skip_frame);
    state->current_packet_first_id = *id;
    state->is_incoming_packet_fec_protected = true;
    // we don't need to free because the core will do it...
//    my_free(cnx, id);
    return (protoop_arg_t) 0;
}