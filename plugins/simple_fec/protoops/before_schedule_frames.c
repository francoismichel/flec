#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"


/**
 * Schedule frames and provide a packet with the path it should be sent on when connection is ready
 * \param[in] packet \b picoquic_packet_t* The packet to be sent
 * \param[in] send_buffer_max \b size_t The maximum amount of bytes that can be written on the packet
 * \param[in] current_time \b uint64_t Time of the scheduling
 * \param[in] retransmit_p \b picoquic_packet_t* A candidate packet for retransmission
 * \param[in] from_path \b picoquic_path_t* The path on which the candidate packet was sent
 * \param[in] reason \b char* A description of the reason for which the candidate packet is proposed
 *
 * \return \b int 0 if everything is ok
 * \param[out] path_x \b picoquic_path_t* The path on which the packet should be sent
 * \param[out] length \b uint32_t The length of the packet to be sent
 * \param[out] header_length \b uint32_t The length of the header of the packet to be sent
 */
protoop_arg_t schedule_frames_on_path(picoquic_cnx_t *cnx)
{
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    // set the current fpid
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    state->has_written_fpi_frame = false;
    state->has_written_repair_frame = false;
    state->has_written_fb_fec_repair_frame = false;
    state->has_written_recovered_frame = false;
    if (state->n_reserved_id_or_repair_frames == 0)
        // we maybe have a completely free slot (should be rare, only at the connection startup)
        fec_check_for_available_slot(cnx, available_slot_reason_none, current_time);
    return 0;
}