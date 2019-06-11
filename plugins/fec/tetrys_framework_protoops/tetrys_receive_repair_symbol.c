#include "../framework/tetrys_framework_receiver.c"


protoop_arg_t receive_repair_symbol(picoquic_cnx_t *cnx)
{
    bpf_state *state = get_bpf_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    return (protoop_arg_t) tetrys_receive_repair_symbol(cnx, state->framework_receiver, (repair_symbol_t *) get_cnx(cnx, AK_CNX_INPUT, 1));
}