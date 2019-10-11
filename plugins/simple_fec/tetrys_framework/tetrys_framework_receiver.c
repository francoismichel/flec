
#include "picoquic.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"
#include "tetrys_framework.h"
#include "wire.h"


#define MIN(a, b) ((a < b) ? a : b)


// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
//FIXME: we pass the state in the parameters because the call to get_bpf_state leads to an error when loading the code
static __attribute__((always_inline)) bool tetrys_receive_source_symbol(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff, tetrys_source_symbol_t *ss) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) return false;
    ssize_t written = 0;
    // set the source fpid of the fpid frame to 0 in the source symbol, because it has been encoded like this (this is weird but with tetrys, we cannot know the sfpid before encoding)
    // (we don't overwrite the type byte)
//    my_memset(&ss->data[FEC_SOURCE_SYMBOL_OVERHEAD + state->sfpid_frame_position_in_current_packet_payload + 1], 0, sizeof(source_fpid_frame_t));
    tetrys_serialize_source_symbol(cnx, ss, ff->buffer, SERIALIZATION_BUFFER_SIZE, &written, state->symbol_size);
    if (send(ff->unix_sock_fd, ff->buffer, written, 0) != written) {
        PROTOOP_PRINTF(cnx, "SERIALIZATION ERROR\n");
        return false;
    }
    delete_tetrys_source_symbol(cnx, ss);
    return true;
}


static __attribute__((always_inline)) int tetrys_receive_packet_payload(picoquic_cnx_t *cnx, tetrys_fec_framework_t *wff,
                                                                        uint8_t *payload, size_t payload_length, uint64_t packet_number, tetrys_source_symbol_id_t first_symbol_id, size_t symbol_size) {
    uint16_t n_chunks = 0;
    source_symbol_t **sss = packet_payload_to_source_symbols(cnx, payload, payload_length, symbol_size, packet_number, &n_chunks, sizeof(tetrys_source_symbol_t));
    if (!sss)
        return PICOQUIC_ERROR_MEMORY;

    for (int i = 0 ; i < n_chunks ; i++) {
        tetrys_source_symbol_t *ss = (tetrys_source_symbol_t *) sss[i];
        ss->id = first_symbol_id + i;
        int err = tetrys_receive_source_symbol(cnx, wff, ss);
        if (err) {
            my_free(cnx, sss);
            return err;
        }
    }
    my_free(cnx, sss);
    return 0;
}


// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int tetrys_receive_repair_symbol(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff, tetrys_repair_symbol_t *rs){
    ssize_t written = 0;
    tetrys_serialize_repair_symbol(cnx, rs, ff->buffer, SERIALIZATION_BUFFER_SIZE, &written);
    ssize_t s;
    if ((s = send(ff->unix_sock_fd, ff->buffer, written, 0)) != written) {
        return false;
    }
    delete_tetrys_repair_symbol(cnx, rs);
    return true;
}

//pre: the rss must have been created as an array of (tetrys_repair_symbol_t *)
static __attribute__((always_inline)) void tetrys_receive_repair_symbols(picoquic_cnx_t *cnx, tetrys_fec_framework_t *wff, repair_symbol_t **rss, uint16_t n_symbols) {
    for (int i = 0 ; i < n_symbols ; i++) {
        if (!tetrys_receive_repair_symbol(cnx, wff, (tetrys_repair_symbol_t *) rss[i])) {
            delete_repair_symbol(cnx, rss[i]);
            rss[i] = NULL;
        }
    }
}


static __attribute__((always_inline)) tetrys_recovered_frame_t *tetrys_get_recovered_frame(picoquic_cnx_t *cnx, tetrys_fec_framework_t *wff, size_t max_bytes) {
    size_t minimum_size = 2*sizeof(uint64_t);
    if (max_bytes < minimum_size) // not enough space
        return NULL;
    tetrys_recovered_frame_t *rf = create_tetrys_recovered_frame(cnx);
    if (!rf)
        return NULL;
    max_bytes -= minimum_size;
    while(!pq_is_empty(wff->recovered_packets) && max_bytes > sizeof(uint64_t) + sizeof(tetrys_source_symbol_id_t)) {
        uint64_t pn = 0;
        uint64_t id = 0;
        pq_get_min_key_val(wff->recovered_packets, &pn, (void **) &id);
        add_packet_to_recovered_frame(cnx, rf, pn, id);
        max_bytes -= sizeof(uint64_t) + sizeof(tetrys_source_symbol_id_t);
        pq_pop_min(wff->recovered_packets);
    }
    return rf;
}




