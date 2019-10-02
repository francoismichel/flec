#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"


/**
 * Returns true if there are protected data to send
 */
protoop_arg_t has_protected_data_to_send(picoquic_cnx_t *cnx)
{
    return helper_find_ready_stream(cnx) != NULL;
}