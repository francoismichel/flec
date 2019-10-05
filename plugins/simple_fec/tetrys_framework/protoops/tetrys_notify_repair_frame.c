#include <picoquic.h>
#include <getset.h>
#include "../wire.h"

// we here assume a single-path context

protoop_arg_t notify_repair_frame(picoquic_cnx_t *cnx)
{
    reserve_frame_slot_t *rfs = (reserve_frame_slot_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    /* Commented out, can be used if needed */
    /* int received = (int) get_cnx(cnx, AK_CNX_INPUT, 1); */
    // TODO: maybe free this before (i.e. just after writing) as it is never retransmitted
    delete_tetrys_repair_frame(cnx, rfs->frame_ctx);
    PROTOOP_PRINTF(cnx, "FREE RFS\n");
    my_free(cnx, rfs);
    return 0;
}