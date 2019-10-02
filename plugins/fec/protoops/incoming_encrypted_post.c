#include "../fec_protoops.h"

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
    bpf_state *state = get_bpf_state(cnx);
    state->current_symbol_length = 0;
    if (state->current_symbol)
        my_free(cnx, state->current_symbol);
    state->current_symbol = NULL;
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    bool slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);
    void *ret = (void *) run_noparam(cnx, "find_ready_stream", 0, NULL, NULL);
    if (!ret && slot_available) {
        PROTOOP_PRINTF(cnx, "no stream data to send, FLUSH RS BY WAKING NOW\n");
        set_cnx(cnx, AK_CNX_WAKE_NOW, 0, 1);
        return 0;
    }
    return 0;
}