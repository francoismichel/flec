
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller.h"

#define SMALL_WINDOW 75
#define PROTOOP_PRINTF2(cnx, fmt, ...)   helper_protoop_printf(cnx, fmt, (protoop_arg_t[]){__VA_ARGS__}, PROTOOP_NUMARGS(__VA_ARGS__))

protoop_arg_t bulk_causal_ew(picoquic_cnx_t *cnx) {
//    PROTOOP_PRINTF(cnx, "BULK EW\n");
    protoop_arg_t loss_rate_times_granularity = 0, gemodel_r_times_granularity = 0;
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t granularity = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    fec_window_t *current_window = (fec_window_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t current_time = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    uint64_t max_proportion = 0;
    uint64_t wsize = window_size(current_window);
    if(!get_loss_parameters(cnx, path, current_time, granularity, &loss_rate_times_granularity, NULL, &gemodel_r_times_granularity)) {
        if (wsize < SMALL_WINDOW) {
            max_proportion = (3*wsize)/4;
        } else {
            max_proportion = (3*wsize)/4;
        }
    } else {
        PROTOOP_PRINTF(cnx, "loss rate times granularity = %lu\n", loss_rate_times_granularity);
        max_proportion = window_size(current_window)*2*MAX(loss_rate_times_granularity, 1*GRANULARITY)/GRANULARITY;
        PROTOOP_PRINTF(cnx, "loss rate times granularity = %lu, window size = %lu, max proportion = %lu\n", loss_rate_times_granularity, window_size(current_window), max_proportion);
    }
    PROTOOP_PRINTF(cnx, "N FEC IN FLIGHT = %lu, WINDOW SIZE = %lu, LIKELY TO BE LOST = %lu, GEMODEL R = %lu, MAX PROPORTION = %lu\n", controller->n_fec_in_flight, wsize, (1 + MAX(granularity/MAX(1, gemodel_r_times_granularity), max_proportion)), gemodel_r_times_granularity, (1 + MAX(granularity/MAX(1, gemodel_r_times_granularity), max_proportion)));
    // we send FEC to cover the number of lost packets i) given the uniform loss rate ii) given the expected Gilbert Model burst size
    uint64_t time_threshold = get_path(path, AK_PATH_SMOOTHED_RTT, 0)/8;    // wait for a silence of 1/8 of RTT
//    uint64_t time_threshold = 0; // test not waiting for starlink
//    PROTOOP_PRINTF2(cnx, "SRTT = %lu, CURRENT = %lu, LAST = %lu, total_threshold = %lu\n", get_path(path, AK_PATH_SMOOTHED_RTT, 0), current_time, controller->last_sent_id_timestamp, controller->last_sent_id_timestamp + time_threshold);
    bool conditions_to_send_fec = !fec_has_protected_data_to_send(cnx) && controller->n_fec_in_flight < (1 + MAX(granularity/MAX(1, gemodel_r_times_granularity), max_proportion));
    if (conditions_to_send_fec) {
        if (current_time >= controller->last_sent_id_timestamp + time_threshold) {
            // waited enough time
            return true;
        } else {
            protoop_arg_t args[2];
            args[0] = current_time;
            args[1] = controller->last_sent_id_timestamp + time_threshold;
            run_noparam(cnx, "request_waking_at_last_at", 2, args, NULL);
        }
    }

    return false;
}