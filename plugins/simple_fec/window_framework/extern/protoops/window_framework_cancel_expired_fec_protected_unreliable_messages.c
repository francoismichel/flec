
#include <picoquic.h>
#include <getset.h>
#include "../../../fec.h"
#include "../../framework_sender.h"


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


/**
 * cancels the expired messages

 */
protoop_arg_t cancel_expired_unreliable_messages(picoquic_cnx_t *cnx)
{
    uint64_t current_time = picoquic_current_time();

    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    window_fec_framework_t *framework = (window_fec_framework_t *) state->framework_sender;
    rbt_key min_key = 0;
    rbt_val min_val = NULL;

    if (rbt_is_empty(cnx, framework->unreliable_messages_from_deadlines))
        return 0;

    while(rbt_min(cnx, framework->unreliable_messages_from_deadlines, &min_key, &min_val) && min_key < current_time) {   // while the tree is not empty and there are expired messages
        unreliable_message_metadata_t *md = (unreliable_message_metadata_t *) min_val;
        // TODO: remove the repetitive call to find_stream, it is awful in terms of performance
        picoquic_stream_head *stream_head = picoquic_find_stream(cnx, md->stream_id, false);
        if (!PSTREAM_FIN_SENT(stream_head)) {
            PROTOOP_PRINTF(cnx, "RESET STREAM %lu\n", md->stream_id);
            picoquic_reset_stream(cnx, md->stream_id, 0);
        }

        rbt_delete_min(cnx, framework->unreliable_messages_from_deadlines);
    }
    return 0;
}