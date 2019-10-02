#include "../framework/window_framework_sender.h"
#include "../framework/window_framework_receiver.h"


protoop_arg_t window_try_to_recover(picoquic_cnx_t *cnx)
{
    bpf_state *state = get_bpf_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    try_to_recover(cnx, state, state->framework_receiver);
    return 0;
}