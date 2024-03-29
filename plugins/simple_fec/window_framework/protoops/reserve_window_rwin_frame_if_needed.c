#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_receiver.h"

// schedule_frames_on_path
// TODO: we here assume a single-path context
protoop_arg_t reserve_window_rwin_frame_if_needed_protoop(picoquic_cnx_t *cnx) {
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    picoquic_packet_context_enum pc = picoquic_packet_context_application;
    picoquic_state_enum cnx_state = (picoquic_state_enum) get_cnx(cnx, AK_CNX_STATE, 0);
    bool ready = cnx_state == picoquic_state_client_ready || cnx_state == picoquic_state_server_ready;
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    wff->a_window_frame_has_been_written = false;   // reset the value
    bool should_retransmit = (wff->a_window_frame_has_been_lost)
                                || (wff->last_acknowledged_smallest_considered_id == 0 && helper_is_tls_stream_ready(cnx));  // flush while crypto frames to send
    if (ready && (should_retransmit || wff->smallest_considered_id_for_which_rwin_frame_has_been_sent != wff->smallest_considered_id_to_advertise
        || helper_is_ack_needed(cnx, current_time, pc, (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0)))) {
        reserve_window_rwin_frame_if_needed(cnx, wff);
        wff->a_window_frame_has_been_lost = false;
    }
    return 0;
}