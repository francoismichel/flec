
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../causal_redundancy_controller_protoops/causal_redundancy_controller_general.h"


protoop_arg_t bulk_causal_ew(picoquic_cnx_t *cnx) {
    PROTOOP_PRINTF(cnx, "BULK EW\n");
    protoop_arg_t loss_rate_times_granularity = 0, gemodel_r_times_granularity = 0;
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    causal_redundancy_controller_t *controller = (causal_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t granularity = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    fec_window_t *current_window = (fec_window_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t current_time = (protoop_arg_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    get_loss_parameters(cnx, path, current_time, granularity, &loss_rate_times_granularity, NULL, &gemodel_r_times_granularity);
    PROTOOP_PRINTF(cnx, "N FEC IN FLIGHT = %lu, LIKELY TO BE LOST = %lu, GEMODEL R = %lu\n", controller->n_fec_in_flight, (1 + MAX(granularity/MAX(1, gemodel_r_times_granularity), window_size(current_window)*loss_rate_times_granularity/GRANULARITY)), gemodel_r_times_granularity);
    // we send FEC to cover the number of lost packets i) given the uniform loss rate ii) given the expected Gilbert Model burst size
    return !fec_has_protected_data_to_send(cnx) && controller->n_fec_in_flight < (1 + MAX(granularity/MAX(1, gemodel_r_times_granularity), window_size(current_window)*loss_rate_times_granularity/GRANULARITY));
}