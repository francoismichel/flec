

#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec.h"
#include "../framework_sender.h"

/**
 * Prepare a STREAM frame.
 * \param[in] stream \b picoquic_stream_head* The stream from which data to write originate
 * \param[in] bytes \b uint8_t* Pointer to the buffer to write the frame
 * \param[in] bytes_max \b size_t Max size that can be written
 *
 * \return \b int Error code, 0 means it's ok
 * \param[out] consumed \b size_t Number of bytes written
 */
protoop_arg_t prepare_stream_frame(picoquic_cnx_t *cnx)
{
    int ret = get_cnx(cnx, AK_CNX_RETURN_VALUE, 0);
    if (ret != 0) {
        return ret;
    }
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        PROTOOP_PRINTF(cnx, "out of memory\n");
        return PICOQUIC_ERROR_MEMORY;
    }
    uint8_t *out_buffer = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    size_t bytes_max = get_cnx(cnx, AK_CNX_INPUT, 2);
    size_t stream_frame_size = get_cnx(cnx, AK_CNX_OUTPUT, 0);

    if (stream_frame_size <= 0) {
        PROTOOP_PRINTF(cnx, "wrong written stream frame size\n");
        return ret;
    }

/**
 *  return_values must contain 5 pointers to:
 *
 *  uint64_t* stream_id
 *  uint64_t* offset
 *  size_t* data_length
 *  int* fin
 *  size_t* consumed
 */

    uint64_t stream_id = 0;
    uint64_t offset = 0;
    size_t data_length = 0;
    int fin = 0;
    size_t consumed = 0;

    protoop_arg_t *return_values[5] = {(protoop_arg_t *) &stream_id, (protoop_arg_t *) &offset, (protoop_arg_t *) &data_length, (protoop_arg_t *) &fin, (protoop_arg_t *) &consumed};

    helper_parse_stream_header(out_buffer, bytes_max, return_values);

    window_fec_framework_t *framework = (window_fec_framework_t *) state->framework_sender;

    // XXX this must be reset by the window framework after being used !
    // FIXME: ugly and specific to streams, while we would like to protect any kind of frame
    framework->min_deadline_in_current_packet = protected_stream_chunks_get_deadline_for_chunk(cnx, &framework->stream_chunks_queue, stream_id, offset, data_length);
    PROTOOP_PRINTF(cnx, "DEADLINE FOR CHUNK %lu = %lu\n", offset, framework->min_deadline_in_current_packet);

    return 0;
}