
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
    uint8_t *bytes = my_malloc(cnx, 1 + sizeof(uint64_t) + (uint16_t) get_ph(ph, AK_PH_PAYLOAD_LENGTH));
    if (!bytes) {
        return PICOQUIC_ERROR_MEMORY;
    }
//    my_memcpy(bytes + 1 + sizeof(uint64_t), bytes_protected + (uint16_t) get_ph(ph, AK_PH_OFFSET), (uint16_t) get_ph(ph, AK_PH_PAYLOAD_LENGTH));
//    encode_u64(get_ph(ph, AK_PH_SEQUENCE_NUMBER), bytes + 1);
//    bytes[0] = FEC_MAGIC_NUMBER;

    protoop_arg_t args[4];
    args[0] = (protoop_arg_t) bytes_protected + get_ph(ph, AK_PH_OFFSET);
    args[1] = (protoop_arg_t) bytes;
    args[2] = get_ph(ph, AK_PH_PAYLOAD_LENGTH);
    args[3] = get_ph(ph, AK_PH_SEQUENCE_NUMBER);

    PROTOOP_PRINTF(cnx, "INCOMING, pn = 0x%x, data[9] = 0x%x, PAYLOAD LENGTH = %d, offset = %d\n", bytes[8], bytes[9], (uint16_t) get_ph(ph, AK_PH_PAYLOAD_LENGTH), (uint16_t) get_ph(ph, AK_PH_OFFSET));
    state->current_packet = bytes;
    state->current_packet_length = get_ph(ph, AK_PH_PAYLOAD_LENGTH);
    state->current_packet_number = get_ph(ph, AK_PH_SEQUENCE_NUMBER);
    state->is_incoming_packet_fec_protected = false;
    return 0;
}