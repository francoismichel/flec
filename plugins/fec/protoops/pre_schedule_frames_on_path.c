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
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    picoquic_packet_t *retransmit_p = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 3);
    picoquic_path_t *retransmit_path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 4);
    if (get_pkt(packet, AK_PKT_CONTEXT) != picoquic_packet_context_application) {
        PROTOOP_PRINTF(cnx, "WRONG CONTEXT, CONTEXT IS %d\n", get_pkt(packet, AK_PKT_CONTEXT));
        return 0;
    }
    PROTOOP_PRINTF(cnx, "PKT CONTEXT IS %d\n", get_pkt(packet, AK_PKT_CONTEXT));
    bpf_state *state = get_bpf_state(cnx);
    state->current_sfpid_frame = NULL;
    state->current_packet_contains_fpid_frame = false;
    state->current_packet_contains_fec_frame = false;
    state->cancel_sfpid_in_current_packet = false;
    state->has_ready_stream = (void *) run_noparam(cnx, "find_ready_stream", 0, NULL, NULL) != NULL;

    if (((window_fec_framework_t *) state->framework_sender)->current_slot > MAX_SLOT_VALUE){
        PROTOOP_PRINTF(cnx, "MAXIMUM NUMBER OF SLOTS EXCEEDED\n");
        return -1;
    }

    window_detect_lost_protected_packets(cnx, current_time, picoquic_packet_context_application);

    // FIXME: with the following lines, we assume that there is only one path
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    bool slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);

    PROTOOP_PRINTF(cnx, "CWIN = %u, BIT = %u\n", get_path(path, AK_PATH_CWIN, 0), get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0));
    if (slot_available) {
        causal_packet_type_t what_to_send = window_what_to_send(cnx, state->framework_sender);
        PROTOOP_PRINTF(cnx, "WHAT TO SEND = %d, SLOT AVAILABLE = %d, READY STREAM = %d\n", what_to_send, slot_available, state->has_ready_stream);
        switch(what_to_send) {
//            case fec_packet:    // version only fb-fec
            case new_rlnc_packet:
                if (!state->sfpid_reserved && state->has_ready_stream) {    // there is no frame currently reserved, so reserve one to protect this packet
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
                    if (ret) {
                        return ret;
                    }

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
                } else PROTOOP_PRINTF(cnx, "ELSE !\n");  // else, an SFPID frame is already reserved, so we keep the frame that is currently reserved
                // if we did not have data to send, then fallthrough and send a repair symbol
                if (state->has_ready_stream)
                    break;
            case fec_packet:  // full version
            case fb_fec_packet:;
            if (state->handshake_finished || state->n_fec_frames_sent_before_handshake_finished <  MAX_FEC_FRAMES_SENT_BEFORE_HANDSHAKE_FINISHED) {
                PROTOOP_PRINTF(cnx, "TRY TO RESERVE FEC FRAME, WINDOW_EMPTY = %d, LENGTH = %d\n", is_fec_window_empty(state->framework_sender), ((window_fec_framework_t *) state->framework_sender)->window_length);
                if (is_fec_window_empty(state->framework_sender))
                    break;
                uint8_t nss = 0;
                repair_symbol_t *rs = get_one_coded_symbol(cnx, state->framework_sender, &nss, what_to_send);
                if (!rs)
                    return PICOQUIC_ERROR_MEMORY;
                reserve_fec_frame_for_repair_symbol(cnx, state->framework_sender, PICOQUIC_MAX_PACKET_SIZE, rs, nss);
                my_free(cnx, rs);
                if (!state->handshake_finished)
                    state->n_fec_frames_sent_before_handshake_finished++;
                // we don't free the rs payload for the moment as it will be used for the frame
                break;
            }
            default:
                break;
        }

    } else {
        PROTOOP_PRINTF(cnx, "NO SLOT AVAILABLE\n");
    }


    return 0;
}