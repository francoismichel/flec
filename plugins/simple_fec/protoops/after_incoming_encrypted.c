
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
        uint16_t n_symbols = 0;
        source_symbol_t **symbols = packet_payload_to_source_symbols(cnx, state->current_packet, state->current_packet_length, SYMBOL_SIZE, state->current_packet_number, &n_symbols);
        if (!symbols || n_symbols == 0)
            return 0;
        protoop_arg_t args[2];
        args[0] = (protoop_arg_t) symbols;
        args[1] = (protoop_arg_t) n_symbols;
        err = (int) run_noparam(cnx, FEC_PROTOOP_RECEIVE_SOURCE_SYMBOLS, 2, args, NULL);
    }
    return err;
}