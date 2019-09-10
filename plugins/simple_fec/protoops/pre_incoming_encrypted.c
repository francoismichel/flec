
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
    uint8_t* bytes_protected = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0); //cnx->protoop_inputv[0];
    picoquic_packet_header* ph = (picoquic_packet_header *) get_cnx(cnx, AK_CNX_INPUT, 1); //cnx->protoop_inputv[1];
    plugin_state_t *state = get_plugin_state(cnx);

    state->current_packet = bytes_protected;
    state->current_packet_length = get_ph(ph, AK_PH_PAYLOAD_LENGTH);
    state->current_packet_number = get_ph(ph, AK_PH_SEQUENCE_NUMBER);
    state->is_incoming_packet_fec_protected = false;
    return 0;
}