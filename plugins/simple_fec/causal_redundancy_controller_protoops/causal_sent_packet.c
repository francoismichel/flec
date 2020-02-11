
#include <picoquic.h>
#include <getset.h>
#include "../fec.h"
#include "../window_framework/types.h"
#include "causal_redundancy_controller.h"
#include "../window_framework/framework_sender.h"



protoop_arg_t causal_sent_packet(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;
//    uint64_t packet_number = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    picoquic_packet_t *packet = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    bool fec_protected = (bool) get_cnx(cnx, AK_CNX_INPUT, 1);
    bool contains_repair_frame = (bool) get_cnx(cnx, AK_CNX_INPUT, 2);
    bool is_fb_fec = (bool) get_cnx(cnx, AK_CNX_INPUT, 3);
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 4);
    window_source_symbol_id_t first_id = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID);
    uint16_t n_source_symbols = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS);
    causal_packet_type_t ptype;
    if (fec_protected)
        ptype = new_rlnc_packet;
    else if (is_fb_fec)
        ptype = fb_fec_packet;
    else
        ptype = fec_packet;
    window_packet_metadata_t md;
    my_memset(&md, 0, sizeof(md));
    if (fec_protected) {
        md.source_metadata.first_symbol_id = first_id;
        md.source_metadata.number_of_symbols = n_source_symbols;
        PROTOOP_PRINTF(cnx, "SENT PROTECTED PACKET, SLOT = %d, [%u, %u]\n", get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT), md.source_metadata.first_symbol_id, md.source_metadata.first_symbol_id + md.source_metadata.number_of_symbols);
    }
    if (contains_repair_frame) {
        md.repair_metadata.first_protected_source_symbol_id = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_FIRST_PROTECTED_SYMBOL_ID);
        md.repair_metadata.n_protected_source_symbols = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_NUMBER_OF_PROTECTED_SYMBOLS);
        md.repair_metadata.number_of_repair_symbols = get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_NUMBER_OF_REPAIR_SYMBOLS);
        md.repair_metadata.is_fb_fec = is_fb_fec;
        PROTOOP_PRINTF(cnx, "SENT REPAIR PACKET, SLOT = %d, [%u, %u[\n", get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT), md.repair_metadata.first_protected_source_symbol_id, md.repair_metadata.first_protected_source_symbol_id + md.repair_metadata.n_protected_source_symbols);
    }
    sent_packet(cnx, path, ((window_fec_framework_t *) state->framework_sender)->controller, ptype, get_pkt_metadata(cnx, packet, FEC_PKT_METADATA_SENT_SLOT), md);
    return 0;
}