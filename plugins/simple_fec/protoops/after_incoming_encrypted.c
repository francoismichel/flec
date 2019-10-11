
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"

/**
 * uint8_t* bytes = (uint8_t *) cnx->protoop_inputv[0];
 * picoquic_packet_header* ph = (picoquic_packet_header *) cnx->protoop_inputv[1];
 * struct sockaddr* addr_from = (struct sockaddr *) cnx->protoop_inputv[2];
 * uint64_t current_time = (uint64_t) cnx->protoop_inputv[3];
 *
 * Output: return code (int)
 */
protoop_arg_t incoming_encrypted(picoquic_cnx_t *cnx)
{
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    int err = 0;
    if (state->is_incoming_packet_fec_protected) {
        err = receive_packet_payload(cnx, state->current_packet, state->current_packet_length,
                                         state->current_packet_number, state->current_packet_first_id);
    }
    fec_after_incoming_packet(cnx);
    return err;
}