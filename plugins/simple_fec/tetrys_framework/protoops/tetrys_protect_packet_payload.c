#include "../tetrys_framework_sender.c"


protoop_arg_t fec_protectpacket_payload(picoquic_cnx_t *cnx)
{
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    uint8_t *payload = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    size_t payload_length = (size_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t packet_number = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    source_symbol_id_t first_symbol_id = 0;
    uint16_t n_symbols = 0;

    size_t tmp = 0;
    int err = tetrys_protect_packet_payload(cnx, state, (tetrys_fec_framework_sender_t *) state->framework_sender, payload, payload_length,
                                            packet_number, &first_symbol_id, &n_symbols, state->symbol_size);
    PROTOOP_PRINTF(cnx, "DONE PROTECTED, SERIALIZE ID %u\n", first_symbol_id);
    // rewrite the src fpi frame with the correct value
    err = serialize_tetrys_fpi_frame(((tetrys_fec_framework_sender_t *) state->framework_sender)->address_of_written_fpi_frame_payload,
                                      sizeof(tetrys_source_symbol_id_t), first_symbol_id, &tmp, state->symbol_size);
    PROTOOP_PRINTF(cnx, "RETURNED %d\n", err);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, first_symbol_id);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, n_symbols);

    return err;
}