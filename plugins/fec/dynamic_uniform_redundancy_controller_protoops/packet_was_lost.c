#include "dynamic_uniform_redundancy_controller.h"
#include "../fec_protoops.h"

// sets as output the pointer towards the controller's state
protoop_arg_t packet_was_lost(picoquic_cnx_t *cnx)
{
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;
    dynamic_uniform_redundancy_controller_t *urc = state->controller;
    // the computed formula is :
    // loss rate = (1-alpha)*loss_rate + alpha*1 = (1-alpha)*loss_rate + alpha
    // the granularity is present to avoid using floats that are forbidden in eBPF
    // we do the division at the end to avoid imprecisions
    urc->loss_rate_times_granularity = (urc->loss_rate_times_granularity - (urc->loss_rate_times_granularity)/ALPHA_DENOMINATOR) + GRANULARITY/ALPHA_DENOMINATOR;
    return 0;
}