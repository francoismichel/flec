
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"



protoop_arg_t bulk_causal_ew(picoquic_cnx_t *cnx) {
    PROTOOP_PRINTF(cnx, "BULK EW\n");
    return !fec_has_protected_data_to_send(cnx);
}