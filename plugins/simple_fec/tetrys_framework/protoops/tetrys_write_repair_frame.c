#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../wire.h"

// we here assume a single-path context

protoop_arg_t write_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    if(state->has_written_fpi_frame)
        return PICOQUIC_MISCCODE_RETRY_NXT_PKT;
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    tetrys_repair_frame_t *rf = (tetrys_repair_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 2);

    my_memset(bytes, FRAME_REPAIR, 1);
    bytes++;

    size_t consumed = 0;
    int err = serialize_tetrys_repair_frame(cnx, bytes, bytes_max - bytes, rf, state->symbol_size, &consumed);
    if (!err) {
        state->has_written_fb_fec_repair_frame = rf->is_fb_fec;
        state->has_written_repair_frame = !rf->is_fb_fec;
        // TODO: we set the two first variables to 0 but it should be ok because this is only a feature proposed by the core plugin, not an obligation to fullfill
        state->current_repair_frame_first_protected_id = 0;//rf->first_protected_symbol;
        state->current_repair_frame_n_protected_symbols = 0;//rf->n_protected_symbols;
        state->current_repair_frame_n_repair_symbols = rf->n_repair_symbols;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 1 + consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) true);   // FIXME we make it retransmittable but we disable the retransmissions afterwards  but otherwise we can't process it
    PROTOOP_PRINTF(cnx, "WRITTEN REPAIR FRAME\n");
    return err;
}