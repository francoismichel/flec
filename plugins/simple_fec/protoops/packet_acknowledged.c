#include "picoquic.h"
#include "plugin.h"
#include "../../helpers.h"
#include "../fec.h"

/**
 * See PROTOOP_NOPARAM_PROCESS_ACK_RANGE
 */
protoop_arg_t packet_acknowledged(picoquic_cnx_t *cnx)
{
    picoquic_packet_t *p = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);

    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    uint64_t sequence_number = get_pkt(p, AK_PKT_SEQUENCE_NUMBER);
    uint64_t send_time = get_pkt(p, AK_PKT_SEND_TIME);
    int metadata_idx[4];
    metadata_idx[0] = FEC_PKT_METADATA_FLAGS;
    metadata_idx[1] = FEC_PKT_METADATA_SENT_SLOT;
    metadata_idx[2] = FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID;
    metadata_idx[3] = FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS;

    protoop_arg_t outs[4];
    get_pkt_n_metadata(cnx, p, metadata_idx, 4, outs);
#define pkt_flags           outs[0]
#define pkt_slot            outs[1]
#define pkt_id              outs[2]
#define pkt_n_symbols       outs[3]

    bool fec_protected = FEC_PKT_IS_FEC_PROTECTED(pkt_flags);
    bool contains_repair_frame = FEC_PKT_CONTAINS_REPAIR_FRAME(pkt_flags);
    if (fec_protected || contains_repair_frame) {
        fec_packet_symbols_have_been_received(cnx, sequence_number, pkt_slot, pkt_id, pkt_n_symbols,
                                              fec_protected, contains_repair_frame, send_time, current_time, &state->pid_received_packet);
    }

    return (protoop_arg_t) 0;
}
