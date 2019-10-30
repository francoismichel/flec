#include <picoquic.h>
#include "../types.h"
#include "../framework_sender.h"

// we here assume a single-path context

protoop_arg_t window_packet_sent(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
    picoquic_packet_t *packet = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    bool fec_protected = (bool) get_cnx(cnx, AK_CNX_INPUT, 1);
    bool contains_repair_frame = (bool) get_cnx(cnx, AK_CNX_INPUT, 2);
    bool is_fb_fec = (bool) get_cnx(cnx, AK_CNX_INPUT, 3);
    if (fec_protected) {
        window_fec_framework_t *wff = (window_fec_framework_t *) state->framework_sender;
        window_source_symbol_id_t first_id = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID);
        uint16_t n_symbols = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS);
        window_source_symbol_id_t supposed_id;
        get_next_source_symbol_id(cnx, (framework_sender_t) wff, (source_symbol_id_t *) &supposed_id);
        PROTOOP_PRINTF(cnx, "EVENT::{\"time\": %ld, \"type\": \"sent_protected_packet\", \"pn\": %ld, \"id\": %u}\n", picoquic_current_time(), get_pkt(packet, AK_PKT_SEQUENCE_NUMBER), first_id);
        if (supposed_id != first_id) {
            PROTOOP_PRINTF(cnx, "THE WRONG ID HAS BEEN PUT INTO THE PACKET ! %u INSTEAD OF %u\n", first_id, supposed_id);
            return -1;
        }
        wff->max_id = first_id + n_symbols - 1;
    } else if (contains_repair_frame) {
        window_source_symbol_id_t first = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_PROTECTED_SYMBOL_ID);
        uint16_t n_protected = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_NUMBER_OF_PROTECTED_SYMBOLS);
        PROTOOP_PRINTF(cnx, "EVENT::{\"time\": %ld, \"type\": \"sent_repair_packet\", \"pn\": %ld, \"window\": [%u, %u]}\n", picoquic_current_time(), get_pkt(packet, AK_PKT_SEQUENCE_NUMBER), first, first+n_protected-1);
    }
    return 0;
}