#include "../framework/window_framework_sender.h"
#include "../framework/window_framework_receiver.h"


protoop_arg_t fec_protect_source_symbol(picoquic_cnx_t *cnx)
{
    window_fec_framework_t *wff = (window_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    source_symbol_t *ss = (source_symbol_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    bpf_state *state = get_bpf_state(cnx);
    if (!state)
        return -1;
    protoop_arg_t retval = (protoop_arg_t) protect_source_symbol(cnx, wff, ss);
    if (retval == 0) {
        state->last_protected_slot = wff->current_slot;
        sfpid_takes_off(wff, ss->source_fec_payload_id);
    }
    return retval;
}