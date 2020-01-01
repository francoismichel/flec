
#include <picoquic.h>
#include <getset.h>
#include "../../../fec.h"
#include "../../framework_sender.h"

/**
 * uint64_t stream_id the id of the stream on which to send the data,
   const uint8_t* data the data buffer to send
   size_t length the size of the data buffer to send
   int set_fin  set to true when it is the last message to send on this stream
   uint64_t data_lifetime the amount of time during which the data can be used if received by the peer (so it is useless to be transmitted if the deadline is bigger than the one-way delay)

   currently can only handle streams until ID WINDOW_FEC_FRAMEWORK_MAX_HANDLED_STREAM_ID
 */
protoop_arg_t window_framework_send_fec_protected_message(picoquic_cnx_t *cnx)
{
    uint64_t current_time = picoquic_current_time();

    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    int64_t stream_id = get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* data = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    size_t length = get_cnx(cnx, AK_CNX_INPUT, 2);
    int set_fin = (int) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t data_lifetime = get_cnx(cnx, AK_CNX_INPUT, 4);
    uint64_t message_deadline_timestamp_microsec = current_time + data_lifetime;
    if (stream_id > WINDOW_FEC_FRAMEWORK_MAX_HANDLED_STREAM_ID) {
        PROTOOP_PRINTF(cnx, "cannot handle stream IDs of more than 15\n");
        return -1;
    }
    window_fec_framework_t *framework = (window_fec_framework_t *) state->framework_sender;
    int ret = picoquic_add_to_stream(cnx, stream_id, data, length, set_fin);
    if (ret == 0) {
        int64_t current_offset = framework->stream_enqueued_data_amount[stream_id];
        framework->stream_enqueued_data_amount[stream_id] += length;
        protected_stream_chunks_queue_add(cnx, &framework->stream_chunks_queue, stream_id, current_offset, length, message_deadline_timestamp_microsec);
    }
    return 0;
}