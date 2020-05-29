
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"


protoop_arg_t sent_packet(picoquic_cnx_t *cnx) {
    bool fec_protected = (bool) get_cnx(cnx, AK_CNX_INPUT, 1);
    // input[2] = current_time
    bool contains_repair_frame = (bool) get_cnx(cnx, AK_CNX_INPUT, 3);
    bool is_fb_fec = (bool) get_cnx(cnx, AK_CNX_INPUT, 4);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return -1;
    if (fec_protected || (contains_repair_frame && is_fb_fec))
        state->n_repair_frames_sent_since_last_feedback = 0;    // new feedback
    return 0;
}