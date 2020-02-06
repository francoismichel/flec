
#include <picoquic.h>
#include <getset.h>
#include <stdint.h>
#include "../helpers.h"



#define CWIN 300000

/**
 * See PROTOOP_NOPARAM_CONGESTION_ALGORITHM_NOTIFY
 */
protoop_arg_t congestion_algorithm_notify(picoquic_cnx_t *cnx)
{
    picoquic_path_t* path_x = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t configured_cwin = get_cnx_metadata(cnx, 0);
    uint64_t cwin = (configured_cwin != 0) ? configured_cwin : CWIN;
    set_path(path_x, AK_PATH_CWIN, 0, cwin);
    PROTOOP_PRINTF(cnx, "EVENT::{\"time\": %ld, \"type\": \"cwin\", \"cwin\": %lu}\n", picoquic_current_time(), cwin);
    return 0;
}
