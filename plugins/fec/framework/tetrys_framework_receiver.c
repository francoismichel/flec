
#include "../bpf.h"
#include "picoquic.h"
#include "../../helpers.h"
#include "memory.h"
#include "memcpy.h"
#include "tetrys_framework.h"


#define MIN(a, b) ((a < b) ? a : b)

static __attribute__((always_inline)) void remove_and_free_repair_symbols(picoquic_cnx_t *cnx, fec_block_t *fb){

}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
static __attribute__((always_inline)) int tetrys_receive_repair_symbol(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff, repair_symbol_t *rs){
    ssize_t written = 0;
    tetrys_serialize_repair_symbol(cnx, rs, ff->buffer, SERIALIZATION_BUFFER_SIZE, &written);
    ssize_t s;
    if ((s = send(ff->unix_sock_fd, ff->buffer, written, 0)) != written) {
        return false;
    }
    free_repair_symbol(cnx, rs);
    return true;
}

// returns true if the symbol has been successfully processed
// returns false otherwise: the symbol can be destroyed
//FIXME: we pass the state in the parameters because the call to get_bpf_state leads to an error when loading the code
static __attribute__((always_inline)) bool tetrys_receive_source_symbol(picoquic_cnx_t *cnx, tetrys_fec_framework_t *ff, source_symbol_t *ss){
    bpf_state *state = get_bpf_state(cnx);
    if (!state || state->in_recovery) return false;
    ssize_t written = 0;
    // set the source fpid of the fpid frame to 0 in the source symbol, because it has been encoded like this (this is weird but with tetrys, we cannot know the sfpid before encoding)
    // (we don't overwrite the type byte)
    my_memset(&ss->data[FEC_SOURCE_SYMBOL_OVERHEAD + state->sfpid_frame_position_in_current_packet_payload + 1], 0, sizeof(source_fpid_frame_t));
    tetrys_serialize_source_symbol(cnx, ss, ff->buffer, SERIALIZATION_BUFFER_SIZE, &written);
    if (send(ff->unix_sock_fd, ff->buffer, written, 0) != written) {
        PROTOOP_PRINTF(cnx, "SERIALIZATION ERROR\n");
        return false;
    }
    free_source_symbol(cnx, ss);
    return true;
}