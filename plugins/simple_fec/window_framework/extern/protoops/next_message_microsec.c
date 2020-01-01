
#include <picoquic.h>
#include <getset.h>
#include "../../../fec.h"
#include "../../framework_sender.h"

/**
 * int64_t next_message_microsec: the number of microsec before the next message sent by the application
 *                            -1 if you don't know
 */
protoop_arg_t next_message_microsec(picoquic_cnx_t *cnx)
{
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    int64_t next_message_microsec = (int64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (next_message_microsec > 0) {
        wff->next_message_timestamp_microsec = picoquic_current_time() + next_message_microsec;
    }
    return 0;
}