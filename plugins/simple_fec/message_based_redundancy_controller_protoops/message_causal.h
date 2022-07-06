#ifndef MESSAGE_CAUSAL_H
#define MESSAGE_CAUSAL_H

#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"


typedef struct {
    uint64_t last_fully_protected_message_deadline;
    uint64_t last_fully_protected_window_end_id;
    uint64_t last_packet_since_ew;
    uint64_t n_ew_for_last_packet;
    uint64_t max_trigger;
} message_causal_addon_t;




static __attribute__((always_inline)) message_causal_addon_t *get_message_addon_state(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller) {

    message_causal_addon_t *addon_state = (message_causal_addon_t *) controller->causal_addons_states[0];

    if (!addon_state) {
        addon_state = my_malloc(cnx, sizeof(message_causal_addon_t));
        if (!addon_state) {
            PROTOOP_PRINTF(cnx, "ERROR: COULD NOT ALLOCATE ADDON STATE\n");
            return NULL;
        }
        my_memset(addon_state, 0, sizeof(message_causal_addon_t));
        addon_state->last_fully_protected_message_deadline = UNDEFINED_SYMBOL_DEADLINE;
        controller->causal_addons_states[0] = (protoop_arg_t) addon_state;
    }
    return addon_state;

}
static __attribute__((always_inline)) int64_t get_max_fec_threshold(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller, fec_window_t *current_window, picoquic_path_t *path, uint64_t current_time, uint64_t granularity) {
    protoop_arg_t loss_rate_times_granularity = 0, gemodel_r_times_granularity = 0;
    get_loss_parameters(cnx, path, current_time, granularity, &loss_rate_times_granularity, NULL, &gemodel_r_times_granularity);
//    return MAX(3*window_size(current_window)/2, 1);
    return (1 + MAX(granularity/MAX(1, gemodel_r_times_granularity), window_size(current_window)*loss_rate_times_granularity/GRANULARITY));
}

#endif // MESSAGE_CAUSAL_H