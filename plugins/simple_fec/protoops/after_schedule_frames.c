#include <picoquic.h>
#include <getset.h>
#include "../../helpers.h"
#include "../fec_constants.h"
#include "../fec.h"


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
    plugin_state_t *state = get_plugin_state(cnx);


    // protect the source symbol
    uint8_t *data = (uint8_t *) get_pkt(packet, AK_PKT_BYTES);
    picoquic_packet_type_enum packet_type = retransmit_p ? get_pkt(retransmit_p, AK_PKT_TYPE) : get_pkt(packet, AK_PKT_TYPE);
    uint32_t header_length = get_pkt(packet, AK_PKT_OFFSET);

    protoop_arg_t packet_flags = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FLAGS);

    if (state->has_written_fpi_frame && (packet_type == picoquic_packet_1rtt_protected_phi0 || packet_type == picoquic_packet_1rtt_protected_phi1)){
        // copy the packet payload without the header and put it 8 bytes after the start of the buffer

        uint16_t n_symbols = 0;
        source_symbol_id_t id = 0;
        int err = protect_packet_payload(cnx, data + header_length, length - header_length, get_pkt(packet, AK_PKT_SEQUENCE_NUMBER), &id, &n_symbols);

        if (err != 0 && err != 1)
            return err;

        if (err == 0) {
            PROTOOP_PRINTF(cnx, "THE PACKET HAS BEEN SUCCESSFULLY PROTECTED\n");
            // something has been protected
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT, state->current_slot++);
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FLAGS, packet_flags | FEC_PKT_METADATA_FLAG_IS_FEC_PROTECTED);
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID, id);
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS, n_symbols);
            fec_sent_packet(cnx, packet, true, false, false);
        }

    } else if (state->has_written_repair_frame || state->has_written_fb_fec_repair_frame) {
        set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT, state->current_slot++);
        set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FLAGS, packet_flags | FEC_PKT_METADATA_FLAG_CONTAINS_REPAIR_FRAME);
        if (state->has_written_fb_fec_repair_frame)
            set_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FLAGS, packet_flags | FEC_PKT_METADATA_FLAG_IS_FB_FEC);
        fec_sent_packet(cnx, packet, false, true, state->has_written_fb_fec_repair_frame);
    }
    state->has_written_fpi_frame = false;
    state->has_written_repair_frame = false;
    state->has_written_fb_fec_repair_frame = false;
    // maybe wake now if slots are still available
//    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
//    bool slot_available = get_path(path, AK_PATH_CWIN, 0) > get_path(path, AK_PATH_BYTES_IN_TRANSIT, 0);
//    if (!is_buffer_empty(((window_fec_framework_t *) state->framework_sender)->controller->what_to_send) && slot_available) {
//        set_cnx(cnx, AK_CNX_WAKE_NOW, 0, 1);
//    }
    return 0;
}