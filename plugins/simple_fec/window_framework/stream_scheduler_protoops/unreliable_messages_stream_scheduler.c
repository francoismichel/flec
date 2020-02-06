
#include <picoquic.h>
#include <getset.h>
#include "../red_black_tree.h"
#include "../../window_framework/framework_sender.h"


// not sure this will be the selected path but gives a lower bound to the sending time of the message
// returns NULL if no available path exists
static __attribute__((always_inline)) picoquic_path_t *get_lowest_rtt_available_non_cwin_limited(picoquic_cnx_t *cnx) {
    int nb_paths = get_cnx(cnx, AK_CNX_NB_PATHS, 0);
    picoquic_path_t *best_path = NULL;
    uint64_t lowest_rtt = UINT64_MAX;
    for (int i = 0 ; i < nb_paths ; i++) {
        picoquic_path_t *candidate = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, i);
        uint64_t candidate_rtt = get_path(candidate, AK_PATH_SMOOTHED_RTT, 0);
        if (candidate_rtt < lowest_rtt && get_path(candidate, AK_PATH_BYTES_IN_TRANSIT, 0) < get_path(candidate, AK_PATH_CWIN, 0)) {
            best_path = candidate;
            lowest_rtt = candidate_rtt;
        }
    }

    // TODO: see if this below makes sense
    // no best path found, maybe that all paths are cwin-limited, by default send the path 0
    if (!best_path) {
        best_path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    }
    return best_path;
}

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


// verifies if the stream can be scheduled
static __attribute__((always_inline)) bool stream_can_be_scheduled(picoquic_stream_head *current_stream) {
    picoquic_stream_data *send_queue = (picoquic_stream_data *) get_stream_head(current_stream, AK_STREAMHEAD_SEND_QUEUE);
    uint64_t sent_offset = get_stream_head(current_stream, AK_STREAMHEAD_SENT_OFFSET);
    uint64_t max_data_remote = get_stream_head(current_stream, AK_STREAMHEAD_MAX_DATA_REMOTE);
    return (send_queue != NULL && get_stream_data(send_queue, AK_STREAMDATA_LENGTH) > get_stream_data(send_queue, AK_STREAMDATA_OFFSET) &&
            sent_offset < max_data_remote) ||
           (PSTREAM_SEND_FIN(current_stream) && (sent_offset < max_data_remote) && !PSTREAM_FIN_SENT(current_stream)) ||
           (PSTREAM_SEND_RESET(current_stream) && !PSTREAM_RESET_SENT(current_stream)) ||
           (PSTREAM_SEND_STOP_SENDING(current_stream) && !PSTREAM_STOP_SENDING_SENT(current_stream) && !PSTREAM_FIN_RCVD(current_stream) && !PSTREAM_RESET_RCVD(current_stream));
}




/**
 * See PROTOOP_NOPARAM_FIND_READY_STREAM
 *
 *
 *
 * !!!!!!!!!! must be attached to find_ready_stream and schedule_next_stream
 *
 *
 *
 */
protoop_arg_t deadline_scheduler(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    picoquic_stream_head *best_stream = NULL;

    if (state && get_cnx(cnx, AK_CNX_MAXDATA_REMOTE, 0) > get_cnx(cnx, AK_CNX_DATA_SENT, 0)) {
        window_fec_framework_t *framework = (window_fec_framework_t *) state->framework_sender;
        if (!rbt_is_empty(framework->unreliable_messages_from_deadlines)) {
            symbol_deadline_t current_deadline = UNDEFINED_SYMBOL_DEADLINE;
            picoquic_path_t *lowest_rtt_path = get_lowest_rtt_available_non_cwin_limited(cnx);

            // I'm *so* tired of this clunky logic...

            // search the stream with the soonest deadline that has the characteristics needed to be scheduled
            while(!best_stream) {
                rbt_key soonest_deadline;
                rbt_val soonest_deadline_stream_metadata;
                // we estimate the owd by RTT/2 and cross our fingers
                uint64_t one_way_delay = get_path(lowest_rtt_path, AK_PATH_SMOOTHED_RTT, 0)/2;
                symbol_deadline_t deadline_to_ceil = current_deadline == UNDEFINED_SYMBOL_DEADLINE ? (picoquic_current_time() + one_way_delay) : current_deadline;
                bool found_ceiling = rbt_ceiling(framework->unreliable_messages_from_deadlines, deadline_to_ceil,
                                                 &soonest_deadline, &soonest_deadline_stream_metadata);
                if (!found_ceiling) {
                    break;
                }
                current_deadline = soonest_deadline + 1;
                picoquic_stream_head *current_stream = picoquic_find_stream(cnx, ((unreliable_message_metadata_t *) soonest_deadline_stream_metadata)->stream_id, false);
                if (stream_can_be_scheduled(current_stream)) {
                    best_stream = current_stream;
                }

            }
        }
    }

    if (!best_stream) {
        // if we didn't find anything interesting
        return find_ready_stream_round_robin(cnx);
    }
    return (protoop_arg_t) best_stream;
}
