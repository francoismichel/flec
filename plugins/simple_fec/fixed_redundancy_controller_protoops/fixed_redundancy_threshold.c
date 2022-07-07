
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"



protoop_arg_t bulk_causal_threshold(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t granularity = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    uint64_t current_time = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 3);
    return (protoop_arg_t) -(1*GRANULARITY - r_times_granularity(controller));
}