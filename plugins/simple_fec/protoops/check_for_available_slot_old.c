#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"

// we here assume a single-path context

protoop_arg_t check_for_available_slot(picoquic_cnx_t *cnx) {
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    protoop_arg_t slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);
    if (slot_available) {
        // there is a slot available
        // we have the guarantee that if we reserve a frame (sufficiently small), it will be put in the packet if we
        // are not rate-limited
        return (int) run_noparam(cnx, FEC_PROTOOP_AVAILABLE_SLOT, 1, &slot_available, NULL);
    }
    // no slot available
    return 0;
}