
#include <picoquic.h>
#include <getset.h>
#include <stdint.h>
#include "../helpers.h"


/**
 * See PROTOOP_NOPARAM_CONGESTION_ALGORITHM_NOTIFY
 */
protoop_arg_t set_cwin(picoquic_cnx_t *cnx)
{
    uint64_t cwin = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    set_cnx_metadata(cnx, 0, cwin);
    return 0;
}
