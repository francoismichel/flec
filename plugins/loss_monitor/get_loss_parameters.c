#include <picoquic.h>
#include "loss_monitor.h"

/**
 *
 * @return 0 if no error, !0 otherwise
 *         in the outputs, you will find:
 *          - granularity the granularity for the values
 *          - uniform_loss_rate_times_granularity
 *          - gmodel_p_times_granularity
 *          - gmodel_r_times_granularity
 */

protoop_arg_t protoop_get_loss_parameters(picoquic_cnx_t *cnx) {

    PROTOOP_PRINTF(cnx, "MONITOR GET LOSS\n");

    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t granularity = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);

    loss_monitor_t *monitor = (loss_monitor_t *) get_path_metadata(cnx, path, PATH_METADATA_LOSS_MONITOR_IDX);
    PROTOOP_PRINTF(cnx, "GET PARAMS GOT MONITOR %p\n", (protoop_arg_t) monitor);
    if (!monitor) {
        monitor = my_malloc(cnx, sizeof(loss_monitor_t));
        PROTOOP_PRINTF(cnx, "MALLOC NEW MONITOR\n");
        if (!monitor) {
            return PICOQUIC_ERROR_MEMORY;
        }
        loss_monitor_init(cnx, monitor, current_time, DEFAULT_ESTIMATION_GRANULARITY, DEFAULT_ESTIMATION_INTERVAL_MICROSEC, DEFAULT_EVENT_LIFETIME_MICROSEC);
        set_path_metadata(cnx, path, PATH_METADATA_LOSS_MONITOR_IDX, (protoop_arg_t) monitor);
    }

    PROTOOP_PRINTF(cnx, "UPDATE DIFF = %lu, MAX UPDATE = %lu\n", current_time - monitor->last_estimation_update_timestamp, monitor->estimations_update_interval);
    if (current_time - monitor->last_estimation_update_timestamp >= monitor->estimations_update_interval) {
        update_estimations(cnx, monitor, granularity, current_time);
    }

    PROTOOP_PRINTF(cnx, "GET LOSS PARAMS: GRAN = %lu, UNI = %lu, P = %lu, R = %lu\n", monitor->estimations.granularity,
                                                                                      monitor->estimations.uniform_rate_times_granularity,
                                                                                      monitor->estimations.p_times_granularity,
                                                                                      monitor->estimations.r_times_granularity);

    set_cnx(cnx, AK_CNX_OUTPUT, 0, monitor->estimations.uniform_rate_times_granularity);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, monitor->estimations.p_times_granularity);
    set_cnx(cnx, AK_CNX_OUTPUT, 2, monitor->estimations.r_times_granularity);
    PROTOOP_PRINTF(cnx, "MONITOR GET LOSS END\n");

    return monitor->has_done_estimations;
}