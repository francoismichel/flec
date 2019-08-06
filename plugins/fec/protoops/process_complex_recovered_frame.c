#include "picoquic.h"
#include "../fec_protoops.h"
#include "../framework/window_framework_sender.h"


protoop_arg_t process_recovered_frame(picoquic_cnx_t *cnx)
{
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return PICOQUIC_ERROR_MEMORY;
    uint8_t *size_and_packets = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    recovered_packets_t rp;
    rp.number_of_packets = *size_and_packets;           // |X| |     |     |
    rp.number_of_sfpids = *(size_and_packets + 1);      // | |X|     |     |
    rp.packets = (uint64_t *) (size_and_packets+2);     // | | |  X  |     |
    rp.recovered_sfpids = (source_fpid_t *) (size_and_packets + 2 + rp.number_of_packets*sizeof(uint64_t));  // | | |     |  X  |

    PROTOOP_PRINTF(cnx, "PROCESS RECOVERED FRAME NUMBER OF PACKETS = %d, NUMBER OF SFPIDs = %d\n", rp.number_of_packets, rp.number_of_sfpids);

    enqueue_recovered_packets(&state->recovered_packets, &rp);
    // FIXME: when a packet is recovered, sfpid_has_landed() will be called later with a "lost" signal when the packet is detected as lost by the loss recovery mchanism
    // FIXME: (we should not change that, juste take in account the fact that a packet that has been lost might have been recovered (or might be recovered in the future)
    for (int i = 0 ; i < rp.number_of_sfpids ; i++) {
        PROTOOP_PRINTF(cnx, "PROCESS SFPID %u AT %p\n", rp.recovered_sfpids[i].raw, (protoop_arg_t) &rp.recovered_sfpids[i]);
        sfpid_has_landed(cnx, state->framework_sender, rp.recovered_sfpids[i], 1);
    }
    return (protoop_arg_t) 0;
}