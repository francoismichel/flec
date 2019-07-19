#include "picoquic.h"
#include "../framework/window_framework_sender.h"
#include "../fec_protoops.h"


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
    picoquic_packet_t *packet = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    picoquic_packet_t *retransmit_p = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    picoquic_path_t *retransmit_path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 4);

    bpf_state *state = get_bpf_state(cnx);
    state->current_sfpid_frame = NULL;
    state->current_packet_contains_fpid_frame = false;
    state->current_packet_contains_fec_frame = false;
    state->cancel_sfpid_in_current_packet = false;
    state->has_ready_stream = (void *) run_noparam(cnx, "find_ready_stream", 0, NULL, NULL) != NULL;


    causal_packet_type_t what_to_send = window_what_to_send(cnx, state->framework_sender);
    switch(what_to_send) {
        case new_rlnc_packet:
            if (!state->sfpid_reserved) {    // there is no frame currently reserved, so reserve one to protect this packet
                // we need to reserve a new one
                // reserve a new frame to  send a FEC-protected packet
                reserve_frame_slot_t *slot = (reserve_frame_slot_t *) my_malloc(cnx, sizeof(reserve_frame_slot_t));
                if (!slot)
                    return PICOQUIC_ERROR_MEMORY;

                my_memset(slot, 0, sizeof(reserve_frame_slot_t));

                slot->frame_type = SOURCE_FPID_TYPE;
                slot->nb_bytes = 1 + sizeof(source_fpid_frame_t);
                slot->is_congestion_controlled = false;
                slot->low_priority = true;
                source_fpid_frame_t *f = (source_fpid_frame_t *) my_malloc(cnx, sizeof(source_fpid_frame_t));
                if (!f)
                    return PICOQUIC_ERROR_MEMORY;
                slot->frame_ctx = f;

                source_fpid_t s;

                protoop_arg_t ret = set_source_fpid(cnx, &s);
                if (ret)
                    return ret;

                f->source_fpid.raw = s.raw;

                size_t reserved_size = reserve_frames(cnx, 1, slot);

                PROTOOP_PRINTF(cnx, "RESERVE SFPID_FRAME %u, size = %d/%d\n", f->source_fpid.raw, reserved_size, slot->nb_bytes);
                if (reserved_size < slot->nb_bytes) {
                    PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
                    my_free(cnx, f);
                    my_free(cnx, slot);
                    return 1;
                }
                state->sfpid_reserved = true;
            }   // else, an SFPID frame is already reserved, so we keep the frame that is currently reserved
            break;
        case fec_packet:
        case fb_fec_packet:
            ;
            if (is_window_empty(state->framework_sender))
                break;
            uint8_t nss = 0;
            repair_symbol_t *rs = get_one_coded_symbol(cnx, state->framework_sender, &nss);
            if (!rs)
                return PICOQUIC_ERROR_MEMORY;
            reserve_fec_frame_for_repair_symbol(cnx, state->framework_sender, PICOQUIC_MAX_PACKET_SIZE, rs, nss);
            break;
        default:
            break;
    }



    return 0;
}