
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"
#include "message_causal.h"

// attached as a post to causal_what_to_send



static __attribute__((always_inline)) void compute_new_last_fully_protected_message_deadline(picoquic_cnx_t *cnx, window_fec_framework_t *wff,
                                                                                            causal_redundancy_controller_t *controller, message_causal_addon_t *addon_state,
                                                                                            fec_window_t *current_window, picoquic_path_t *path, uint64_t current_time, uint64_t granularity) {

//
////    PROTOOP_PRINTF(cnx, "BEFORE UPDATE MIN = %lu, MAX THRESHOLD = %lu\n", MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight), get_max_fec_threshold(cnx, controller, current_window, path, current_time, granularity));
//    PROTOOP_PRINTF(cnx, "BEFORE UPDATE MIN = %lu, MAX THRESHOLD = %lu\n", MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight), addon_state->max_trigger);
////    if (MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight) >= get_max_fec_threshold(cnx, controller, current_window, path, current_time, granularity) && !rbt_is_empty(wff->symbols_from_deadlines)) {
//    if (false && MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight) >= addon_state->max_trigger && !rbt_is_empty(wff->symbols_from_deadlines)) {
//        PROTOOP_PRINTF(cnx, "NEW UPDATE\n");
//        addon_state->last_fully_protected_message_deadline = rbt_max_key(wff->symbols_from_deadlines);
//    }
}


protoop_arg_t message_causal_update_state(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
    fec_window_t window = get_current_fec_window(cnx, wff);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) wff->controller;

    message_causal_addon_t *addon_state = get_message_addon_state(cnx, controller);

    if(!addon_state) {
        return PICOQUIC_ERROR_MEMORY;
    }

//    compute_new_last_fully_protected_message_deadline(cnx, wff, controller, addon_state, &window, path, picoquic_current_time(), GRANULARITY);


    return 0;
}