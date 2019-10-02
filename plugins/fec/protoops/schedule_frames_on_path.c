#include "picoquic.h"
#include "../fec_protoops.h"
#include "../framework/window_framework_sender.h"


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
    uint32_t length = get_cnx(cnx, AK_CNX_OUTPUT, 1);
    // set the current fpid
    bpf_state *state = get_bpf_state(cnx);


    // protect the source symbol
    uint8_t *data = (uint8_t *) get_pkt(packet, AK_PKT_BYTES);
    picoquic_packet_type_enum packet_type = retransmit_p ? get_pkt(retransmit_p, AK_PKT_TYPE) : get_pkt(packet, AK_PKT_TYPE);
    uint32_t header_length = get_pkt(packet, AK_PKT_OFFSET);


    if (state->current_sfpid_frame && (packet_type == picoquic_packet_1rtt_protected_phi0 || packet_type == picoquic_packet_1rtt_protected_phi1)){
        PROTOOP_PRINTF(cnx, "TRY TO PROTECT, LENGTH  %d, OFFSET = %d, retrans = %d\n", length, header_length, retransmit_p != NULL);
        uint8_t *payload_with_pn = my_malloc(cnx, length - header_length + 1 + sizeof(uint64_t));
        // copy the packet payload without the header and put it 8 bytes after the start of the buffer



        protoop_arg_t args[4];
        args[0] = (protoop_arg_t) data + header_length;
        args[1] = (protoop_arg_t) payload_with_pn;
        args[2] = length - header_length;
        args[3] = get_pkt(packet, AK_PKT_SEQUENCE_NUMBER);
        uint32_t symbol_length = (uint32_t) run_noparam_with_pid(cnx, "packet_payload_to_source_symbol", 4, args, NULL, &state->packet_to_source_symbol_id);

        if (symbol_length <= 1 + sizeof(uint64_t) + 1 + sizeof(source_fpid_frame_t)) {
            // this symbol does not need to be protected: undo
            my_memset(state->written_sfpid_frame, 0, 1 + sizeof(source_fpid_frame_t));
            state->current_sfpid_frame = NULL;
            my_free(cnx, payload_with_pn);
        } else {
            source_fpid_t current_sfpid = state->current_sfpid_frame->source_fpid;
            int err = protect_packet(cnx, &state->current_sfpid_frame->source_fpid, payload_with_pn, symbol_length);
            if (err){
                PROTOOP_PRINTF(cnx, "ERROR WHILE PROTECTING\n");
                my_free(cnx, payload_with_pn);
                return (protoop_arg_t) err;
            }
            if (current_sfpid.raw != state->current_sfpid_frame->source_fpid.raw) {
                // if the protect_packet function changed the sfpid, then we rewrite it now (some schemes only know the fpid after it has been protected)
                encode_u32(state->current_sfpid_frame->source_fpid.raw, state->written_sfpid_frame+1);
            }
            PROTOOP_PRINTF(cnx, "SET LAST CC CONTROLLED TO %lu\n", state->last_protected_slot);
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT, state->last_protected_slot);
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_IS_FEC_PROTECTED, true);
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID, current_sfpid.raw);
            PROTOOP_PRINTF(cnx, "SET PKT %x METADATA SFPID %u, GET %u\n", get_pkt(packet, AK_PKT_SEQUENCE_NUMBER), current_sfpid.raw, get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID));
        }

    } else if (state->current_packet_contains_fec_frame) {
        set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT, state->last_fec_slot);
        set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_CONTAINS_FEC_PACKET, true);
    }
    state->current_sfpid_frame = NULL;
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    bool slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);
    if (!is_buffer_empty(((window_fec_framework_t *) state->framework_sender)->controller->what_to_send) && slot_available) {
        set_cnx(cnx, AK_CNX_WAKE_NOW, 0, 1);
    }
    PROTOOP_PRINTF(cnx, "END SCHEDULE\n");
    return 0;
}