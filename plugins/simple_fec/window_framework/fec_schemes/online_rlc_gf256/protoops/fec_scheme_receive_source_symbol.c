
#include <picoquic.h>
#include "../../../../fec.h"
#include "../../../framework_sender.h"
#include "../../../framework_receiver.h"
#include "../headers/online_gf256_fec_scheme.h"

/**
 *  fec_scheme_receive_source_symbol(picoquic_cnx_t *cnx, online_gf256_fec_scheme_t *fec_scheme, window_source_symbol_t *ss)
 */
protoop_arg_t receive_source_symbol(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        return (protoop_arg_t) NULL;
    }
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    online_gf256_fec_scheme_t *fec_scheme = (online_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    window_source_symbol_t *ss = (window_source_symbol_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    PROTOOP_PRINTF(cnx, "BEFORE CREATE FROM ZEROCOPY\n");

    PROTOOP_PRINTF(cnx, "AFTER CREATE FROM ZEROCOPY\n");

    equation_t *removed = NULL;
    int used_in_system = 0;
    PROTOOP_PRINTF(cnx, "BEFORE WRAPPER RECEIVE SOURCE SYMBOL\n");
    int ret = wrapper_receive_source_symbol(cnx, &fec_scheme->wrapper, ss, &removed, &used_in_system);
    PROTOOP_PRINTF(cnx, "AFTER WRAPPER RECEIVE SOURCE SYMBOL\n");
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) removed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) used_in_system);
    return ret;
}