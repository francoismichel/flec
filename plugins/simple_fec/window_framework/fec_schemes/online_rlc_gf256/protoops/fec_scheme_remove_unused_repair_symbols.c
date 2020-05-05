
#include <picoquic.h>
#include "../../../../fec.h"
#include "../../../framework_sender.h"
#include "../../../framework_receiver.h"
#include "../headers/online_gf256_fec_scheme.h"
#include "../../prng/tinymt32.c"
/**
 *  fec_scheme_receive_repair_symbol(picoquic_cnx_t *cnx, online_gf256_fec_scheme_t *fec_scheme, window_repair_symbol_t *rs)
 */
protoop_arg_t receive_repair_symbol(picoquic_cnx_t *cnx) {
    online_gf256_fec_scheme_t *fec_scheme = (online_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    window_source_symbol_id_t highest_contiguously_received = (window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 1);

    return wrapper_remove_recovered_symbols(cnx, &fec_scheme->wrapper, highest_contiguously_received);
}