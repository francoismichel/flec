
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"

#define SMALL_WINDOW 75
#define PROTOOP_PRINTF2(cnx, fmt, ...)   helper_protoop_printf(cnx, fmt, (protoop_arg_t[]){__VA_ARGS__}, PROTOOP_NUMARGS(__VA_ARGS__))


typedef struct {
    uint64_t last_fully_protected_window_end_id;
    uint64_t last_ew_timestamp;
    uint64_t last_packet_since_ew;
    uint64_t n_ew_for_last_packet;
    uint64_t max_trigger;
} ac_rlnc_addon_t;

static __attribute__((always_inline)) ac_rlnc_addon_t *get_ac_rlnc_addon_state(picoquic_cnx_t *cnx, causal_redundancy_controller_t *controller) {

    ac_rlnc_addon_t *addon_state = (ac_rlnc_addon_t *) controller->causal_addons_states[0];

    if (!addon_state) {
        addon_state = my_malloc(cnx, sizeof(ac_rlnc_addon_t));
        if (!addon_state) {
            PROTOOP_PRINTF(cnx, "ERROR: COULD NOT ALLOCATE AC RLNC ADDON STATE\n");
            return NULL;
        }
        my_memset(addon_state, 0, sizeof(ac_rlnc_addon_t));
        controller->causal_addons_states[0] = (protoop_arg_t) addon_state;
    }
    return addon_state;

}


protoop_arg_t ac_rlnc_causal_ew(picoquic_cnx_t *cnx) {
    PROTOOP_PRINTF(cnx, "AC RLNC EW\n");
    protoop_arg_t loss_rate_times_granularity = 0;
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t granularity = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    fec_window_t *current_window = (fec_window_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t current_time = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    uint64_t max_proportion = 0;
    uint64_t wsize = window_size(current_window);

    ac_rlnc_addon_t *addon_state = get_ac_rlnc_addon_state(cnx, controller);

    if(!addon_state) {
        return PICOQUIC_ERROR_MEMORY;
    }
    int n_non_fully_protected = current_window->end - addon_state->last_fully_protected_window_end_id;
    if(!get_loss_parameters(cnx, path, current_time, granularity, &loss_rate_times_granularity, NULL, NULL)) {
        if (wsize < SMALL_WINDOW) {
            max_proportion = (3*wsize)/4;
        } else {
            max_proportion = (3*wsize)/4;
        }
    } else {
        max_proportion = MAX(1, (n_non_fully_protected*loss_rate_times_granularity)/GRANULARITY);
    }


    protoop_arg_t uniform_loss_rate_times_granularity = 0;
    int n_unprotected = current_window->end - addon_state->last_packet_since_ew;

    uint64_t smoothed_rtt = get_path(path, AK_PATH_SMOOTHED_RTT, 0);

    // in AC-RLNC, EW triggers FEC every RTT
    bool ew = smoothed_rtt < current_time - addon_state->last_ew_timestamp;
    // we send FEC to cover the number of lost packets given the uniform loss rate
    if (ew && n_unprotected > 0) {
        addon_state->n_ew_for_last_packet = 1;
        addon_state->max_trigger = max_proportion;
        addon_state->last_packet_since_ew = current_window->end;
    } else if (ew) {
        if (addon_state->n_ew_for_last_packet <= addon_state->max_trigger) {
            addon_state->n_ew_for_last_packet++;
        } else {
            ew = false;
            addon_state->last_ew_timestamp = current_time;
            addon_state->last_fully_protected_window_end_id = current_window->end;
        }
    }

    return ew;
}