#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_receiver.h"


// ugly copy from picoquic.h ...
typedef enum picoquic_stream_flags {
    picoquic_stream_flag_fin_received = 1,
    picoquic_stream_flag_fin_signalled = 2,
    picoquic_stream_flag_fin_notified = 4,
    picoquic_stream_flag_fin_sent = 8,
    picoquic_stream_flag_reset_requested = 16,
    picoquic_stream_flag_reset_sent = 32,
    picoquic_stream_flag_reset_received = 64,
    picoquic_stream_flag_reset_signalled = 128,
    picoquic_stream_flag_stop_sending_requested = 256,
    picoquic_stream_flag_stop_sending_sent = 512,
    picoquic_stream_flag_stop_sending_received = 1024,
    picoquic_stream_flag_stop_sending_signalled = 2048
} picoquic_stream_flags;

protoop_arg_t is_max_stream_data_frame_required(picoquic_cnx_t *cnx) {
    picoquic_stream_head *stream = (picoquic_stream_head *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t maxdata_local = get_stream_head(stream, AK_STREAMHEAD_MAX_DATA_LOCAL);
    uint64_t consumed_offset = get_stream_head(stream, AK_STREAMHEAD_CONSUMED_OFFSET);
    uint64_t max_stream_receive_window_size = get_cnx(cnx, AK_CNX_MAX_STREAM_RECEIVE_WINDOW_SIZE, 0);
    uint64_t desired_offset = maxdata_local + 2 * consumed_offset;
    uint64_t largest_possible_offset = consumed_offset + MIN(max_stream_receive_window_size, UINT64_MAX - consumed_offset); // avoid overflow
    set_cnx(cnx, AK_CNX_OUTPUT, 0, MIN(desired_offset, largest_possible_offset));
    picoquic_state_enum cnx_state = get_cnx(cnx, AK_CNX_STATE, 0);
    if (false && (cnx_state == picoquic_state_client_ready || cnx_state == picoquic_state_server_ready)) {
        plugin_state_t *state = get_plugin_state(cnx);
        window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
        if (wff->a_window_frame_has_been_written) {
            return true;
        }
    }
    bool should_update = (desired_offset < largest_possible_offset &&  2 * consumed_offset > maxdata_local)   // we're not rwin-limited
                         || (desired_offset >= largest_possible_offset && ((maxdata_local - consumed_offset)) < max_stream_receive_window_size/2);  // we are rwin-limited, only update when 50% of the buffer has been read

    PROTOOP_PRINTF(cnx, "SHOULD UPDATE MAX_STREAM_DATA = %d, desired_offset = %lu, largest_possible_offset = %lu, consumed_offset = %lu, maxdata_local = %lu, max_stream_receive_window_size = %lu\n",
                   should_update, desired_offset, largest_possible_offset, consumed_offset, maxdata_local, max_stream_receive_window_size);
    return (!PSTREAM_FIN_RCVD(stream) && !PSTREAM_RESET_RCVD(stream))
           && should_update;
}