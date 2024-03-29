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


    uint64_t now = picoquic_current_time();
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    picoquic_packet_t* p = ppacket;
    int ret = 0;
    lost_packet_t *closest_lost = NULL;
    closest_lost = get_smallest_lost_packet_equal_or_bigger(cnx, &state->lost_packets, highest - range + 1);
    int64_t sequence_number = -1L;
    if (p != NULL) {
        sequence_number = get_pkt(p, AK_PKT_SEQUENCE_NUMBER);
    }
    /* Compare the range to the retransmit queue */
    while ((closest_lost && closest_lost->pn >= 0) && range > 0) {
        // if p is not null, p is not in the lost packets and vice-versa

        if (highest - range < closest_lost->pn && closest_lost->pn <= highest) {
            // received
            fec_packet_symbols_have_been_received(cnx, closest_lost->pn, closest_lost->slot, closest_lost->id, closest_lost->n_source_symbols, true, false, closest_lost->send_time, current_time, &state->pid_received_packet);
            dequeue_lost_packet(cnx, &state->lost_packets, closest_lost->pn, NULL, NULL, NULL, NULL);
            closest_lost = get_smallest_lost_packet_equal_or_bigger(cnx, &state->lost_packets, highest - range + 1);
        } else {
            break;
        }
    }
    PROTOOP_PRINTF(cnx, "PRE PROCESS ACK RANGE ELAPSED %luµs\n", picoquic_current_time() - now);
    return (protoop_arg_t) ret;
}