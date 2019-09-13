#include "../fec.h"


protoop_arg_t prepare_packet_ready(picoquic_cnx_t *cnx)
{
    uint64_t current_time = get_cnx(cnx, AK_CNX_INPUT, 2);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;

    if (state->recovered_packets.size)
        maybe_notify_recovered_packets_to_everybody(cnx, &state->recovered_packets, current_time);
    return 0;
}