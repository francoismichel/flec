#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../wire.h"
#include "../tetrys_framework_receiver.c"


// we here assume a single-path context

protoop_arg_t process_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    PROTOOP_PRINTF(cnx, "PROCESS FRAME\n");
    tetrys_repair_frame_t *rf = (tetrys_repair_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (!rf)
        return PICOQUIC_ERROR_MEMORY;
    PROTOOP_PRINTF(cnx, "PROCESS RF %p\n", (protoop_arg_t) rf);
    // if we reach here, we know that rf-<symbols has been instantiated
    tetrys_receive_repair_symbols(cnx, (tetrys_fec_framework_t *) state->framework_receiver, rf->symbols, rf->n_repair_symbols);
    // we don't delete nothing, the symbols are used by the framework and the rf itself will be freed by the core...
    PROTOOP_PRINTF(cnx, "END PROCESS\n");
    return (protoop_arg_t) 0;
}