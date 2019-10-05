#include "../tetrys_framework.h"
#include "../tetrys_framework_receiver.c"
#define MIN_BYTES_TO_RETRANSMIT_PROTECT 20

static __attribute__((always_inline)) bool is_mtu_probe(picoquic_packet_t *p, picoquic_path_t *path) {
    if (!p || !path) return false;
    // it is mtu if p->length + p->checksum_overhead > send_path->send_mtu
    return get_pkt(p, AK_PKT_LENGTH) + get_pkt(p, AK_PKT_CHECKSUM_OVERHEAD) > get_path(path, AK_PATH_SEND_MTU, 0);
}

/**
 * Select the path on which the next packet will be sent.
 *
 * \param[in] retransmit_p \b picoquic_packet_t* The packet to be retransmitted, or NULL if none
 * \param[in] from_path \b picoquic_path_t* The path from which the packet originates, or NULL if none
 * \param[in] reason \b char* The reason why packet should be retransmitted, or NULL if none
 *
 * \return \b picoquic_path_t* The path on which the next packet will be sent.
 */
protoop_arg_t schedule_frames_on_path(picoquic_cnx_t *cnx)
{
    plugin_state_t *state = get_plugin_state(cnx);
    tetrys_fec_framework_t *tetrys = (tetrys_fec_framework_t *) state->framework_receiver;
    update_tetrys_state(cnx, tetrys);
    if (!pq_is_empty(tetrys->recovered_packets)) {

        tetrys_recovered_frame_t *rf = tetrys_get_recovered_frame(cnx, tetrys, 200);
        if (rf) {
            reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
            if (!slot) {
                my_free(cnx, rf);
                return PICOQUIC_ERROR_MEMORY;
            }
            my_memset(slot, 0, sizeof(reserve_frame_slot_t));
            slot->frame_type = FRAME_RECOVERED;
            slot->nb_bytes = 200;
            slot->frame_ctx = rf;
            slot->is_congestion_controlled = true;
            if (reserve_frames(cnx, 1, slot) != slot->nb_bytes) {
                return PICOQUIC_ERROR_MEMORY;
            }
            PROTOOP_PRINTF(cnx, "RESERVED RECOVERED FRAME\n");
        } else {
            PROTOOP_PRINTF(cnx, "COULD NOT GET A RF\n");
        }
    }
    return 0;
}