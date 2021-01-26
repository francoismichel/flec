#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"
#define PROTOOP_PRINTF2(cnx, fmt, ...)   helper_protoop_printf(cnx, fmt, (protoop_arg_t[]){__VA_ARGS__}, PROTOOP_NUMARGS(__VA_ARGS__))

// we here assume a single-path context

protoop_arg_t check_for_available_slot(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    available_slot_reason_t reason = (available_slot_reason_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    uint64_t now = picoquic_current_time();
    // we ensure not having too much available slots by taking the already reserved frames as taking a slot of MTU bytes
    protoop_arg_t slot_available = reason == available_slot_reason_nack || get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0) + state->n_reserved_id_or_repair_frames*get_path(path, AK_PATH_SEND_MTU, 0);
    PROTOOP_PRINTF(cnx, "SLOT AVAILABLE = %d, n_reserved = %lu (%u >? %u)\n", slot_available, state->n_reserved_id_or_repair_frames, get_path(path, AK_PATH_CWIN, 0), get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0) + state->n_reserved_id_or_repair_frames*PICOQUIC_MAX_PACKET_SIZE);
    if ((slot_available && state->n_reserved_id_or_repair_frames == 0) || controller_retransmission_needed(cnx)) {
        // there is a slot available
        // we have the guarantee that if we reserve a frame (sufficiently small), it will be put in the packet if we
        // are not rate-limited
        fec_available_slot(cnx, path, reason, current_time);
    }
    PROTOOP_PRINTF(cnx, "CHECK FOR AVAILABLE SLOT ELAPSED = %luµs\n", picoquic_current_time() - now);
    // no slot available
    return 0;
}
