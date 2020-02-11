
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "causal_redundancy_controller_general.h"
#include "../window_framework/framework_sender.h"

protoop_arg_t causal_slot_acked(picoquic_cnx_t *cnx) {
    window_redundancy_controller_t controller = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t slot = (source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    PROTOOP_PRINTF(cnx, "CAUSAL SLOT ACKED, CONTROLLER N FEC = %ld\n", ((causal_redundancy_controller_t *)controller)->n_fec_in_flight);
    slot_acked(cnx, controller, slot);
    return 0;
}