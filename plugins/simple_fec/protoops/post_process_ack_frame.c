#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../utils.h"
#include "../fec.h"
#define PROTOOP_PRINTF2(cnx, fmt, ...)   helper_protoop_printf(cnx, fmt, (protoop_arg_t[]){__VA_ARGS__}, PROTOOP_NUMARGS(__VA_ARGS__))

// we here assume a single-path context
/**
 * Process the parsed frame \p frame whose the type is provided as parameter.
 * \param[in] frame \b void* Pointer to the structure malloc'ed in the context memory containing the frame information. Don't free it.
 * \param[in] current_time \b uint64_t Time of reception of the packet containing that frame
 * \param[in] epoch \b int Epoch of the received packet containing the frame
 * \param[in] path_x \b picoquic_path_t* The path on which the frame was received
 *
 * \return \b int Error code, 0 iff everything is fine.
 */
protoop_arg_t pre_packet_has_been_received(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return -1;
    int err = 0;
//    PROTOOP_PRINTF2(cnx, "POST PROCESS ACK FRAME\n");
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    if ((err = fec_check_for_available_slot(cnx, available_slot_reason_ack, current_time)) != 0)
        return err;
    return err;
}