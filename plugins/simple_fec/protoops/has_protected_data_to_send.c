#include <picoquic.h>
#include <getset.h>
#include <picoquic_internal.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"


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
    return data != NULL && ((!cnx_fc_blocked && get_stream_data(data, AK_STREAMDATA_LENGTH) >= get_stream_data(data, AK_STREAMDATA_OFFSET))
                         || (get_stream_data(data, AK_STREAMDATA_LENGTH) == get_stream_data(data, AK_STREAMDATA_OFFSET)
                                            && (PSTREAM_FIN_NOTIFIED(stream) && !PSTREAM_FIN_SENT(stream)) &&
                                               (PSTREAM_RESET_REQUESTED(stream) && !PSTREAM_RESET_SENT(stream)) &&
                                               (PSTREAM_STOP_SENDING_REQUESTED(stream) && !PSTREAM_STOP_SENDING_SENT(stream))) );
}