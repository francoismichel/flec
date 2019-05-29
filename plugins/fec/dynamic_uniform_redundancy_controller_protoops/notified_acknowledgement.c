#include "dynamic_uniform_redundancy_controller.h"
#include "../bpf.h"

// sets as output the pointer towards the controller's state
protoop_arg_t congestion_alg_notify(picoquic_cnx_t *cnx)
{
    picoquic_congestion_notification_t notification = get_cnx(cnx, AK_CNX_INPUT, 1);

    if (notification == picoquic_congestion_notification_acknowledgement) {
        bpf_state *state = get_bpf_state(cnx);
        if (!state) return PICOQUIC_ERROR_MEMORY;
        dynamic_uniform_redundancy_controller_t *urc = state->controller;
        // the computed formula is :
        // loss rate = (1-alpha)*loss_rate + alpha*0 = (1-alpha)*loss_rate
        // the granularity is present to avoid using floats that are forbidden in eBPF
        urc->loss_rate_times_granularity = (urc->loss_rate_times_granularity - (urc->loss_rate_times_granularity)/ALPHA_DENOMINATOR);
    }
    return 0;
}