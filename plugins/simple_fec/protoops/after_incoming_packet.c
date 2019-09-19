#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"

// This function is only used to allow plugins to attach themselves after the last action performed by the FEC core.

protoop_arg_t after_incoming_packet(picoquic_cnx_t *cnx) {
    return 0;
}