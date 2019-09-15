#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../types.h"
#include "../framework_sender.h"
#include "../framework_receiver.h"

// we here assume a single-path context

protoop_arg_t process_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        return PICOQUIC_ERROR_MEMORY;

    uint8_t *size_and_packets = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    if (!size_and_packets)
        return PICOQUIC_ERROR_MEMORY;
    // if we reach here, we know that rf-<symbols has been instantiated
    process_recovered_packets(cnx, (window_fec_framework_t *) state->framework_sender, size_and_packets);
    // don't need to free, it will be freed by the core...
//    my_free(cnx, size_and_packets);
    return (protoop_arg_t) 0;
}