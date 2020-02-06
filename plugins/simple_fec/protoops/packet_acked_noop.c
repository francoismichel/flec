#include <picoquic.h>
#include "../../helpers.h"

protoop_arg_t packet_acknowledged(picoquic_cnx_t *cnx) {
    PROTOOP_PRINTF(cnx, "PACKET ACKED REPLACE\n");
    return 0;
}