
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "causal_redundancy_controller_only_fb_fec.h"
#include "../window_framework/framework_sender.h"

protoop_arg_t causal_slot_nacked(picoquic_cnx_t *cnx) {
    window_redundancy_controller_t controller = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t slot = (source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    slot_nacked(cnx, controller, slot);
    return 0;
}