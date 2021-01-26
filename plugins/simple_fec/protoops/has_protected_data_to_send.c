#include <picoquic.h>
#include <getset.h>
#include <picoquic_internal.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"


//had to remove this once the stream_flags were gone. Let's see if I can put this back at some point
//#define PSTREAM_FLAGS_RESET_SENT(flags) (((flags) & picoquic_stream_flag_reset_sent) != 0)
//#define PSTREAM_FLAGS_RESET_REQUESTED(flags) (((flags) & picoquic_stream_flag_reset_requested) != 0)
//#define PSTREAM_FLAGS_RESET_RCVD(flags) (((flags) & picoquic_stream_flag_reset_received) != 0)
//#define PSTREAM_FLAGS_SEND_RESET(flags) (PSTREAM_FLAGS_RESET_REQUESTED(flags) && !PSTREAM_FLAGS_RESET_SENT(flags))
//#define PSTREAM_FLAGS_STOP_SENDING_REQUESTED(flags) (((flags) & picoquic_stream_flag_stop_sending_requested) != 0)
//#define PSTREAM_FLAGS_STOP_SENDING_SENT(flags) (((flags) & picoquic_stream_flag_stop_sending_sent) != 0)
//#define PSTREAM_FLAGS_STOP_SENDING_RECEIVED(flags) (((flags) & picoquic_stream_flag_stop_sending_received) != 0)
//#define PSTREAM_FLAGS_SEND_STOP_SENDING(flags) (PSTREAM_FLAGS_STOP_SENDING_REQUESTED(flags) && !PSTREAM_FLAGS_STOP_SENDING_SENT(flags))
//#define PSTREAM_FLAGS_FIN_NOTIFIED(flags) (((flags) & picoquic_stream_flag_fin_notified) != 0)
//#define PSTREAM_FLAGS_FIN_SENT(flags) (((flags) & picoquic_stream_flag_fin_sent) != 0)
//#define PSTREAM_FLAGS_FIN_RCVD(flags) (((flags) & picoquic_stream_flag_fin_received) != 0)
//#define PSTREAM_FLAGS_SEND_FIN(flags) (PSTREAM_FLAGS_FIN_NOTIFIED(flags) && !PSTREAM_FLAGS_FIN_SENT(flags))
//#define PSTREAM_FLAGS_CLOSED(flags) ((PSTREAM_FLAGS_FIN_SENT(flags) || ((flags) & picoquic_stream_flag_reset_received) != 0) && (PSTREAM_FLAGS_RESET_SENT(flags)) || (get_stream_head(stream, AK_STREAMHEAD_STREAM_FLAGS) & picoquic_stream_flag_fin_received) != 0)

#define DEBUG_PROTECT_STREAM_4_ONLY (1)

/**
 * Returns true if there are protected data to send
 */
protoop_arg_t has_protected_data_to_send(picoquic_cnx_t *cnx)
{
    picoquic_stream_head *stream = helper_find_ready_stream(cnx);
    if (stream == NULL) {
        return false;
    }
    picoquic_stream_data *data = (picoquic_stream_data *) get_stream_head(stream, AK_STREAMHEAD_SEND_QUEUE);
    bool cnx_fc_blocked = get_cnx(cnx, AK_CNX_MAXDATA_REMOTE, 0) <= get_cnx(cnx, AK_CNX_DATA_SENT, 0);
//    protoop_arg_t flags = get_stream_head(stream, AK_STREAMHEAD_STREAM_FLAGS);
    return data != NULL && (!DEBUG_PROTECT_STREAM_4_ONLY || (get_stream_head(stream, AK_STREAMHEAD_STREAM_ID) == 4)) && ((!cnx_fc_blocked && get_stream_data(data, AK_STREAMDATA_LENGTH) >= get_stream_data(data, AK_STREAMDATA_OFFSET))
                         || (get_stream_data(data, AK_STREAMDATA_LENGTH) == get_stream_data(data, AK_STREAMDATA_OFFSET)
                                            && (PSTREAM_FIN_NOTIFIED(stream) && !PSTREAM_FIN_SENT(stream)) &&
                                               (PSTREAM_RESET_REQUESTED(stream) && !PSTREAM_RESET_SENT(stream)) &&
                                               (PSTREAM_STOP_SENDING_REQUESTED(stream) && !PSTREAM_STOP_SENDING_SENT(stream))) );
}