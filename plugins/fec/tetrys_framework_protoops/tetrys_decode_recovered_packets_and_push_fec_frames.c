#include "../framework/tetrys_framework.h"
#include "../framework/tetrys_framework_sender.c"
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
    bpf_state *state = get_bpf_state(cnx);
    tetrys_fec_framework_t *tetrys = state->framework_sender;
    update_tetrys_state(cnx, tetrys);
    ssize_t size;
    if (tetrys->buffered_recovered_symbols.size > 0) {
        recovered_packets_t *rp = my_malloc(cnx, sizeof(recovered_packets_t));
        if(rp) {    // if rp is null, this is not a big deal, just don't send the recovered frame
            rp->packets = my_malloc(cnx, tetrys->buffered_recovered_symbols.size*sizeof(uint64_t));
            rp->number_of_packets = 0;
            if (!rp->packets) {
                my_free(cnx, rp);
                rp = NULL;
            }
        }
        while ((size = buffer_dequeue_symbol_payload(cnx, &tetrys->buffered_recovered_symbols, tetrys->buffer, SERIALIZATION_BUFFER_SIZE)) > 0) {
            source_fpid_t sfpid;
            sfpid.raw = decode_u32(tetrys->buffer);
            uint8_t *bytes_symbol = tetrys->buffer;
            bytes_symbol += sizeof(source_fpid_t);
            if (*bytes_symbol != FEC_MAGIC_NUMBER) {
                PROTOOP_PRINTF(cnx, "Error decoding recovered symbol: 0x%hhx should be 0x%hhx\n", (protoop_arg_t) *bytes_symbol, (uint8_t) FEC_MAGIC_NUMBER);
                continue;
            }
            int payload_length = size - sizeof(source_fpid_t) - FEC_SOURCE_SYMBOL_OVERHEAD;
            uint64_t pn = decode_u64(bytes_symbol+1);
            picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
            PROTOOP_PRINTF(cnx,
                           "DECODING FRAMES OF TETRYS RECOVERED SYMBOL: pn = %llx, len_frames = %u, start = 0x%x\n",
                           pn, payload_length, bytes_symbol[FEC_SOURCE_SYMBOL_OVERHEAD]);

            uint8_t *tmp_current_packet = state->current_symbol;
            uint16_t tmp_current_packet_length = state->current_symbol_length;
            // ensure that we don't consider it as a new Soruce Symbol when parsing
            state->current_symbol = bytes_symbol;
            state->current_symbol_length = size - sizeof(source_fpid_t);
            state->in_recovery = true;
            int ret = picoquic_decode_frames_without_current_time(cnx, bytes_symbol + FEC_SOURCE_SYMBOL_OVERHEAD, (size_t) payload_length, 3, path);

            state->current_symbol = tmp_current_packet;
            state->current_symbol_length = tmp_current_packet_length;
            state->in_recovery = false;
            // we should free the recovered symbol: it has been correctly handled when decoding the packet

            if (!ret) {
                PROTOOP_PRINTF(cnx, "DECODED ! \n");
                if (rp) {
                    rp->packets[rp->number_of_packets++] = pn;
                }
            } else {
                PROTOOP_PRINTF(cnx, "ERROR WHILE DECODING: %u ! \n", (uint32_t) ret);
            }
        }

        if (rp) {
            reserve_frame_slot_t *slot = NULL;
            if (should_send_recovered_frames(cnx, rp)) {
                slot = my_malloc(cnx, sizeof(reserve_frame_slot_t));
            }
            if (slot) {
                my_memset(slot, 0, sizeof(reserve_frame_slot_t));
                slot->frame_ctx = rp;
                slot->frame_type = RECOVERED_TYPE;
                slot->nb_bytes = 200; /* FIXME dynamic count */
                size_t reserved_size = reserve_frames(cnx, 1, slot);
                if (reserved_size < slot->nb_bytes) {
                    PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
                    my_free(cnx, rp->packets);
                    my_free(cnx, rp);
                    my_free(cnx, slot);
                }
            } else {
                my_free(cnx, rp->packets);
                my_free(cnx, rp);
            }
        }
    }
    reserve_fec_frames(cnx, tetrys, PICOQUIC_MAX_PACKET_SIZE);
    return 0;
}