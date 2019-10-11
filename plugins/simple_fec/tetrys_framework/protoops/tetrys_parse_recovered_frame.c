#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../../fec.h"
#include "../wire.h"

// we here assume a single-path context

protoop_arg_t parse_frame(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state)
        // there is no mean to alert an error...
        return PICOQUIC_ERROR_MEMORY;


    uint8_t* bytes_protected = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint8_t* bytes_max = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    // type byte
    bytes_protected++;

    size_t consumed = 0;
    uint8_t *size_and_packets = parse_tetrys_recovered_frame(cnx, bytes_protected, bytes_max, &consumed);

    if (!size_and_packets)  // error
        return (protoop_arg_t) NULL;

    PROTOOP_PRINTF(cnx, "PARSED RF, COMSUMED = 1 + %ld\n", consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) size_and_packets);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, false);
    set_cnx(cnx, AK_CNX_OUTPUT, 2, true);
    return (protoop_arg_t) bytes_protected + consumed;
}