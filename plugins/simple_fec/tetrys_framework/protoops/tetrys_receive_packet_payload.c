#include "../tetrys_framework_receiver.c"


protoop_arg_t tetrys_receive_packet_payload_protoop(picoquic_cnx_t *cnx) {

    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    uint8_t *payload = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    size_t payload_length = (size_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t packet_number = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    source_symbol_id_t first_symbol_id = (tetrys_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 3);

    return tetrys_receive_packet_payload(cnx, (tetrys_fec_framework_t *) state->framework_receiver, payload, payload_length,
                                         packet_number, first_symbol_id, state->symbol_size);









}