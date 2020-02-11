
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"
#include "message_causal.h"


protoop_arg_t message_causal_ew(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    PROTOOP_PRINTF(cnx, "MESSAGE EW\n");
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t granularity = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    fec_window_t *current_window = (fec_window_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t current_time = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;

    message_causal_addon_t *addon_state = get_message_addon_state(cnx, controller);

    if(!addon_state) {
        return PICOQUIC_ERROR_MEMORY;
    }

    protoop_arg_t uniform_loss_rate_times_granularity = 0, gemodel_p_times_granularity = 0, gemodel_r_times_granularity = 0;
    get_loss_parameters(cnx, path, current_time, granularity, &uniform_loss_rate_times_granularity, &gemodel_p_times_granularity, &gemodel_r_times_granularity);

    int64_t smoothed_rtt_microsec = get_path(path, AK_PATH_SMOOTHED_RTT, 0);
    int64_t owd_microsec = smoothed_rtt_microsec/2;
    rbt_key soonest_deadline_microsec_key;
    rbt_val soonest_deadline_first_id_val;

    bool found_ceiling = rbt_ceiling(wff->symbols_from_deadlines,
                                     MAX((addon_state->last_fully_protected_message_deadline != UNDEFINED_SYMBOL_DEADLINE) ? (addon_state->last_fully_protected_message_deadline + 1) : 0, current_time + owd_microsec),
                                     &soonest_deadline_microsec_key, &soonest_deadline_first_id_val);

    symbol_deadline_t soonest_deadline_microsec = found_ceiling ? ((symbol_deadline_t) soonest_deadline_microsec_key) : UNDEFINED_SYMBOL_DEADLINE;

    if (!rbt_is_empty(wff->symbols_from_deadlines)) {

        PROTOOP_PRINTF(cnx, "found ceiling to %ld = %d, tree size = %d, max = %ld\n", current_time + owd_microsec, found_ceiling, rbt_size(wff->symbols_from_deadlines), rbt_max_key(wff->symbols_from_deadlines));
    }

    uint64_t next_message_time_to_wait_microsec = 0;
    PROTOOP_PRINTF(cnx, "BEFORE IF\n");
    if (wff->next_message_timestamp_microsec != UNDEFINED_SYMBOL_DEADLINE) {
        next_message_time_to_wait_microsec = (wff->next_message_timestamp_microsec <= current_time) ? 0 : (wff->next_message_timestamp_microsec - current_time);
    }





    // FIXME: wrap-around when the sampling period or bytes sent are too high
    bandwidth_t available_bandwidth_bytes_per_second = get_path((picoquic_path_t *) path, AK_PATH_CWIN, 0)*SECOND_IN_MICROSEC/smoothed_rtt_microsec;
    // we take the between the last sampling point and the current bandwidth induced by the bytes in flight
//    bandwidth_t used_bandwidth_bytes_per_second = MAX(controller->last_bytes_sent_sample*SECOND_IN_MICROSEC/controller->last_bytes_sent_sampling_period_microsec,
//                                                        get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0)*SECOND_IN_MICROSEC/get_path(path, AK_PATH_SMOOTHED_RTT, 0));
    bandwidth_t used_bandwidth_bytes_per_second = get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0)*SECOND_IN_MICROSEC/smoothed_rtt_microsec;
    int64_t bw_ratio_times_granularity = (used_bandwidth_bytes_per_second > 0) ? ((granularity*available_bandwidth_bytes_per_second)/used_bandwidth_bytes_per_second) : 0;

    bool ew = (!fec_has_protected_data_to_send(cnx) && bw_ratio_times_granularity > (granularity + granularity/10));
//    if (ew && current_window->end != addon_state->last_packet_since_ew) {
//        addon_state->n_ew_for_last_packet = 0;
//    }

//    int64_t max_fec_threshold = get_max_fec_threshold(cnx, controller, addon_state, current_window, path, current_time, granularity);
    PROTOOP_PRINTF(cnx, "BEFORE ALLOWED TO SEND\n");
    PROTOOP_PRINTF(cnx, "N FEC IN FLIGHT = %lu\n", controller->n_fec_in_flight);
//    PROTOOP_PRINTF(cnx, "MAX FEC THRESHOLD = %lu\n", max_fec_threshold);
    PROTOOP_PRINTF(cnx, "NEXT TS = %lu\n", wff->next_message_timestamp_microsec);
    bool allowed_to_send_fec_given_deadlines = (soonest_deadline_microsec == UNDEFINED_SYMBOL_DEADLINE
                                                || wff->next_message_timestamp_microsec == UNDEFINED_SYMBOL_DEADLINE
                                                // FIXME:: we cross the fingers here so that there will be no overflow
                                                || current_time + next_message_time_to_wait_microsec + owd_microsec + DEADLINE_CRITICAL_THRESHOLD_MICROSEC >= soonest_deadline_microsec); // if false, that means that we can wait a bit before sending FEC
    PROTOOP_PRINTF(cnx, "AFTER ALLOWED TO SEND\n");
    PROTOOP_PRINTF(cnx, "allowed to send = %d, owd = %lu, current_time = %lu, soonest_deadline = %lu, next_timestamp = %lu\n", allowed_to_send_fec_given_deadlines,
                   owd_microsec, current_time, soonest_deadline_microsec, wff->next_message_timestamp_microsec);

    int n_unprotected = current_window->end - addon_state->last_packet_since_ew;
    bool protect = ew && allowed_to_send_fec_given_deadlines;
    if (protect && n_unprotected > 0) {
        addon_state->n_ew_for_last_packet = 1;
        addon_state->max_trigger = 1+MAX((granularity/MAX(1, gemodel_r_times_granularity)), window_size(current_window)*uniform_loss_rate_times_granularity/GRANULARITY);//MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight) <= max_fec_threshold;
        addon_state->last_packet_since_ew = current_window->end;
    } else if (protect) {
        if (MIN(addon_state->n_ew_for_last_packet, controller->n_fec_in_flight) <= addon_state->max_trigger) {
            addon_state->n_ew_for_last_packet++;
        } else {
            protect = false;
            addon_state->last_fully_protected_message_deadline = rbt_max_key(wff->symbols_from_deadlines);
        }
    }




//    if (addon_state->n_ew_for_last_packet >= max_fec_threshold) {
//        addon_state->last_packet_since_ew = current_window->end;
//    }
//    if (ew && allowed_to_send_fec_given_deadlines){
//        addon_state->n_ew_for_last_packet++;
//    }

    if (soonest_deadline_microsec != UNDEFINED_SYMBOL_DEADLINE && wff->next_message_timestamp_microsec != UNDEFINED_SYMBOL_DEADLINE) {
        protoop_arg_t args[2];
        args[0] = current_time;
        args[1] = 1 + soonest_deadline_microsec - (owd_microsec + DEADLINE_CRITICAL_THRESHOLD_MICROSEC);
        run_noparam(cnx, "request_waking_at_last_at", 2, args, NULL);
    }

    return protect;
}