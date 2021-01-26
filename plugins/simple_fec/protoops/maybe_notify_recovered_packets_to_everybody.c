#include "../fec.h"


protoop_arg_t prepare_packet_ready(picoquic_cnx_t *cnx)
{
    uint64_t current_time = get_cnx(cnx, AK_CNX_INPUT, 2);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;

//    PROTOOP_PRINTF(cnx, "MAYBE NOTIFY PACKETS, SIZE = %lu\n", rbt_size(cnx, &state->recovered_packets_ranges));
    uint64_t now = picoquic_current_time();
    if (!rbt_is_empty(cnx, &state->recovered_packets_ranges))
        maybe_notify_recovered_packets_to_everybody(cnx, &state->recovered_packets_ranges, current_time);
//    PROTOOP_PRINTF(cnx, "NOTIFICATION ELAPSED = %luµs\n", picoquic_current_time() - now);
    return 0;
}