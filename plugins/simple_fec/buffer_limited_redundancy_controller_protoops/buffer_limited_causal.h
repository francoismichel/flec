#ifndef buffer_limited_cauSAL_H
#define buffer_limited_cauSAL_H

#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller_general.h"


typedef struct {
    uint64_t last_ew_triggered_microsec;
    uint64_t last_packet_since_ew;
    uint64_t n_ew_for_last_packet;
} buffer_limited_causal_addon_t;




static __attribute__((always_inline)) buffer_limited_causal_addon_t *get_buffer_limited_addon_state(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, uint64_t current_time) {

    buffer_limited_causal_addon_t *addon_state = (buffer_limited_causal_addon_t *) controller->causal_addons_states[0];

    if (!addon_state) {
        addon_state = my_malloc(cnx, sizeof(buffer_limited_causal_addon_t));
        if (!addon_state) {
            PROTOOP_PRINTF(cnx, "ERROR: COULD NOT ALLOCATE ADDON STATE\n");
            return NULL;
        }
        my_memset(addon_state, 0, sizeof(buffer_limited_causal_addon_t));
        addon_state->last_ew_triggered_microsec = current_time;
        controller->causal_addons_states[0] = (protoop_arg_t) addon_state;
    }
    return addon_state;

}


#endif // buffer_limited_cauSAL_H