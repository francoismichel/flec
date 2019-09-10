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

    window_source_symbol_id_t *id = (window_source_symbol_id_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (!id)
        return PICOQUIC_ERROR_MEMORY;
    state->current_packet_first_id = *id;
    my_free(cnx, id);
    return (protoop_arg_t) 0;
}