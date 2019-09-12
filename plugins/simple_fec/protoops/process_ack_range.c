#include "picoquic.h"
#include "plugin.h"
#include "../../helpers.h"
#include "../fec.h"

/**
 * See PROTOOP_NOPARAM_PROCESS_ACK_RANGE
 */
protoop_arg_t process_ack_range(picoquic_cnx_t *cnx)
{
    picoquic_packet_context_enum pc = (picoquic_packet_context_enum) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t highest = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t range = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    picoquic_packet_t* ppacket = (picoquic_packet_t*) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 4);

    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    picoquic_packet_t* p = ppacket;
    int ret = 0;
    int64_t first_lost;
    /* Compare the range to the retransmit queue */
    while ((p != NULL || ((first_lost = get_first_lost_packet(cnx, &state->lost_packets)) <= highest && first_lost >= 0)) && range > 0) {
        uint64_t slot;
        source_symbol_id_t id;
        uint16_t n_source_symbols = 0;
        // TODO: move this to when we handle the recovered packets
        bool present = dequeue_lost_packet(cnx, &state->lost_packets, highest, &slot, &id, &n_source_symbols);
        PROTOOP_PRINTF(cnx, "IS %x IN LOST PACKETS ? %d\n", highest, present);
        if (present) {
            fec_packet_has_been_received(cnx, highest, slot, id, n_source_symbols, true, false);
            // the packet was detected as lost, but it has finally been received (probably through coding)
            PROTOOP_PRINTF(cnx, "END ACKED, p = %p\n", (protoop_arg_t) p);
            PROTOOP_PRINTF(cnx, "[[PACKET RECEIVED]] %lu\n", highest);
            range--;
            highest--;
        } else if (p != NULL) {
            uint64_t sequence_number = get_pkt(p, AK_PKT_SEQUENCE_NUMBER);
            if (sequence_number > highest) {
                p = (picoquic_packet_t*) get_pkt(p, AK_PKT_NEXT_PACKET);
            } else {
                if (sequence_number == highest) {
                    /* TODO: RTT Estimate */
                    picoquic_packet_t* next = (picoquic_packet_t*) get_pkt(p, AK_PKT_NEXT_PACKET);
                    picoquic_path_t * old_path = (picoquic_path_t *) get_pkt(p, AK_PKT_SEND_PATH);

                    uint32_t length = (uint32_t) get_pkt(p, AK_PKT_LENGTH);
                    if ((picoquic_congestion_algorithm_t *) get_cnx(cnx, AK_CNX_CONGESTION_CONTROL_ALGORITHM, 0) != NULL) {
                        helper_congestion_algorithm_notify(cnx, old_path,
                                                           picoquic_congestion_notification_acknowledgement, 0, length, 0, current_time);
                    }
                    bool fec_protected = FEC_PKT_IS_FEC_PROTECTED(get_pkt_metadata(cnx, p, FEC_PKT_METADATA_FLAGS));
                    bool contains_repair_frame = FEC_PKT_CONTAINS_REPAIR_FRAME(get_pkt_metadata(cnx, p, FEC_PKT_METADATA_FLAGS));
                    if (fec_protected || contains_repair_frame) {
                        slot = get_pkt_metadata(cnx, p, FEC_PKT_METADATA_SENT_SLOT);
                        id = (source_symbol_id_t) get_pkt_metadata(cnx, p, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID);
                        n_source_symbols = (uint16_t) get_pkt_metadata(cnx, p, FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS);
                        fec_packet_has_been_received(cnx, sequence_number, slot, id, n_source_symbols, fec_protected, contains_repair_frame);
                        PROTOOP_PRINTF(cnx, "[[PACKET RECEIVED]] %lu,%lu\n", sequence_number, current_time - get_pkt(p, AK_PKT_SEND_TIME));
                    }

                    /* If the packet contained an ACK frame, perform the ACK of ACK pruning logic */
                    helper_process_possible_ack_of_ack_frame(cnx, p);

                    uint32_t old_path_send_mtu = (uint32_t) get_path(old_path, AK_PATH_SEND_MTU, 0);

                    /* If packet is larger than the current MTU, update the MTU */
                    uint32_t checksum_overhead = (uint32_t) get_pkt(p, AK_PKT_CHECKSUM_OVERHEAD);
                    if ((length + checksum_overhead) > old_path_send_mtu) {
                        set_path(old_path, AK_PATH_SEND_MTU, 0, (protoop_arg_t)(length + checksum_overhead));
                        set_path(old_path, AK_PATH_MTU_PROBE_SENT, 0, 0);
                    }

                    /* Any acknowledgement shows progress */
                    picoquic_packet_context_t *pkt_ctx = (picoquic_packet_context_t *) get_path(old_path, AK_PATH_PKT_CTX, pc);
                    set_pkt_ctx(pkt_ctx, AK_PKTCTX_NB_RETRANSMIT, 0);

                    helper_dequeue_retransmit_packet(cnx, p, 1);
                    picoquic_state_enum cnx_state = get_cnx(cnx, AK_CNX_STATE, 0);
                    if (!state->handshake_finished) {
                        if ((cnx_state == picoquic_state_client_ready || cnx_state == picoquic_state_server_ready) &&
                            !get_pkt_ctx(pkt_ctx, AK_PKTCTX_RETRANSMIT_OLDEST)) {
                            // the handshake is now finished
                            state->handshake_finished = true;
                        }
                    }
                    p = next;
                }

                range--;
                highest--;
            }
        } else if (get_first_lost_packet(cnx, &state->lost_packets) <= highest) {

            range--;
            highest--;
        }
    }

    ppacket = p;

    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) ppacket);
    return (protoop_arg_t) ret;
}