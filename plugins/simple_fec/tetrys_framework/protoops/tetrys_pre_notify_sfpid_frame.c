#include "../tetrys_framework_sender.c"

protoop_arg_t framework_notify_sfpid_frame(picoquic_cnx_t *cnx)
{
    reserve_frame_slot_t *rfs = (reserve_frame_slot_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    tetrys_id_has_landed((tetrys_fec_framework_sender_t *) state->framework_sender, ((tetrys_source_symbol_id_t) rfs->frame_ctx));
    return 0;
}