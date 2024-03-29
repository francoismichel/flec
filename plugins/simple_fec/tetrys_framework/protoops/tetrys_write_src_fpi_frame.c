#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../wire.h"
#include "../tetrys_framework.h"

// we here assume a single-path context

protoop_arg_t write_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state){
        PROTOOP_PRINTF(cnx, "MEMORY ERROR WHEN WRITING SRC FPI FRAME\n");
        return PICOQUIC_ERROR_MEMORY;
    }
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    protoop_arg_t slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);
    tetrys_fec_framework_sender_t *ff = (tetrys_fec_framework_sender_t *) state->framework_sender;
    if (state->has_written_fb_fec_repair_frame || state->has_written_repair_frame || state->has_written_fpi_frame) {
        PROTOOP_PRINTF(cnx, "RETRY FPI: HAS WRITTEN FB FEC = %d, HAS WRITTEN REPAIR = %d!!\n", state->has_written_fb_fec_repair_frame, state->has_written_repair_frame);
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 0);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
        return PICOQUIC_MISCCODE_RETRY_NXT_PKT;
    } else if (!slot_available || !run_noparam(cnx, FEC_PROTOOP_HAS_PROTECTED_DATA_TO_SEND, 0, NULL, NULL)) {
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 0);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
        PROTOOP_PRINTF(cnx, "CANCEL THE SRC FPI\n");
        return 0;
    }
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    source_symbol_id_t id = 0;
    //int err = get_next_source_symbol_id(cnx, state->framework_sender, &id); //(window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 2);

    my_memset(bytes, FRAME_FEC_SRC_FPI, bytes_max - bytes);
    bytes++;

    ff->address_of_written_fpi_frame_payload = bytes;
    size_t consumed = 0;
    int err = serialize_tetrys_fpi_frame(bytes, bytes_max - bytes, id, &consumed, state->symbol_size);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 1 + consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
    return err;
}