#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"

// we here assume a single-path context

protoop_arg_t check_for_available_slot(picoquic_cnx_t *cnx) {
    // TODO: call this function with no_feedback when "no frame is reserved but we are in schedule_frames_on_path"
    // TODO: when checking if a slot is available, we must take into account the already reserved frames, as it virtually takes place in the CWIN
    available_slot_reason_t reason = (available_slot_reason_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    protoop_arg_t slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);
    if (slot_available) {
        // there is a slot available
        // we have the guarantee that if we reserve a frame (sufficiently small), it will be put in the packet if we
        // are not rate-limited
        fec_available_slot(cnx, path, reason);
    }
    // no slot available
    return 0;
}