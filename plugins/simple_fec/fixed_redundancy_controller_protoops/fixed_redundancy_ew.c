
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"

#define N_SOURCE_SYMBOLS_BEFORE_REPAIR_SYMBOL 10

typedef struct {
    uint64_t last_packet_since_ew;
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

    protoop_arg_t uniform_loss_rate_times_granularity = 0;
    int n_unprotected = current_window->end - addon_state->last_packet_since_ew;


    bool ew = n_unprotected > N_SOURCE_SYMBOLS_BEFORE_REPAIR_SYMBOL;
    if (ew) {
        addon_state->last_packet_since_ew = current_window->end;
    }

    return ew;
}