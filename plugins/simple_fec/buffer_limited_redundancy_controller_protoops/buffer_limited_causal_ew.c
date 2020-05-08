
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"
#include "buffer_limited_causal.h"

//#define ALLOWED_BUFFER_FOR_FEC 15000
protoop_arg_t bulk_causal_ew(picoquic_cnx_t *cnx) {
    PROTOOP_PRINTF(cnx, "BUFFER-LIMITED EW!\n");
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t granularity = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    fec_window_t *current_window = (fec_window_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t current_time = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    protoop_arg_t uniform_loss_rate_times_granularity = 0, gemodel_p_times_granularity = 0, gemodel_r_times_granularity = 0;
    get_loss_parameters(cnx, path, current_time, granularity, &uniform_loss_rate_times_granularity, &gemodel_p_times_granularity, &gemodel_r_times_granularity);
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        PROTOOP_PRINTF(cnx, "COULD NOT ALLOCATE THE PLUGIN STATE\n");
        return PICOQUIC_ERROR_MEMORY;
    }
    buffer_limited_causal_addon_t *addon = get_buffer_limited_addon_state(cnx, controller, current_time);
    if (!addon) {
        PROTOOP_PRINTF(cnx, "COULD NOT ALLOCATE THE PLUGIN ADDON STATE\n");
        return PICOQUIC_ERROR_MEMORY;
    }

    int n_unprotected = current_window->end - addon->last_packet_since_ew;
    bool fc_blocked = !fec_has_protected_data_to_send(cnx);
    PROTOOP_PRINTF(cnx, "UNIFORM LOSS RATE TIMES RGRANULARITY = %lu, WINDOW SIZE = %lu\n", uniform_loss_rate_times_granularity, window_size(current_window));
    PROTOOP_PRINTF(cnx, "1/r = %lu, max_trigger = %lu\n", granularity/MAX(1, gemodel_r_times_granularity), addon->max_trigger);
    if ((fc_blocked && (n_unprotected == 0 && addon->n_ew_for_last_packet >= addon->max_trigger && controller->n_fec_in_flight >= 2*MAX(1, uniform_loss_rate_times_granularity*window_size(current_window)/granularity))) || (!fc_blocked && controller->n_fec_in_flight >= 2*MAX(1, uniform_loss_rate_times_granularity*window_size(current_window)/granularity))) {
        return false;
    }

//    uint64_t smoothed_rtt_microsec = get_path(path, AK_PATH_SMOOTHED_RTT, 0);
//    bool one_rtt_has_elapsed = addon->last_ew_triggered_microsec + smoothed_rtt_microsec/4 <= current_time;
//    uint64_t n_symbols_likely_to_be_lost = 1+MAX((gemodel_r_times_granularity == GRANULARITY) ? 0 : (granularity/MAX(1, gemodel_r_times_granularity)), n_unprotected*uniform_loss_rate_times_granularity/GRANULARITY);


//    bool enough_packets_sent = n_unprotected == 0 ||  n_unprotected >= MAX(10, window_size(current_window)/MAX(1, n_symbols_likely_to_be_lost));
    bool enough_packets_sent = n_unprotected == 0 ||  n_unprotected >= MIN(window_size(current_window), granularity/(MAX(1, gemodel_p_times_granularity)));

//    uint64_t max_allowed_fec_in_flight = ALLOWED_BUFFER_FOR_FEC/state->symbol_size;
    // TODO: maybe replace size(current_window) by window->end - addon->last_packet_since_ew

    bool should_send_fec = enough_packets_sent;// && controller->n_fec_in_flight < MIN(max_allowed_fec_in_flight, n_symbols_likely_to_be_lost);


    if (should_send_fec) {
//        addon->last_ew_triggered_microsec = current_time;
//        addon->last_packet_since_ew = current_window->end;
    }

    bool protect = fc_blocked || should_send_fec;
    if (protect && n_unprotected == 0) {

//        if (fc_blocked || MIN(addon->n_ew_for_last_packet, controller->n_fec_in_flight) < n_symbols_likely_to_be_lost/*MIN(max_allowed_fec_in_flight, n_symbols_likely_to_be_lost)*/) {
        if (fc_blocked || MIN(addon->n_ew_for_last_packet, controller->n_fec_in_flight) < addon->max_trigger) {
            addon->n_ew_for_last_packet++;
        } else {
            // cancel the EW trigger such that we do not send too much FEC per EW
            PROTOOP_PRINTF(cnx, "CANCEL EW\n");
            protect = false;
        }
    } else if (protect) {
        addon->n_ew_for_last_packet = 1;
        addon->max_trigger = 1+MAX((gemodel_r_times_granularity == GRANULARITY) ? 0 : (granularity/MAX(1, gemodel_r_times_granularity)), n_unprotected*uniform_loss_rate_times_granularity/GRANULARITY);//MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight) <= max_fec_threshold;
        addon->last_packet_since_ew = current_window->end;
    }
    // we send FEC to cover the number of lost packets i) given the uniform loss rate ii) given the expected Gilbert Model burst size
//    return (!fec_has_protected_data_to_send(cnx) && controller->n_fec_in_flight < MIN(max_allowed_fec_in_flight, n_symbols_likely_to_be_lost)) || should_send_fec;
    return protect;
}
