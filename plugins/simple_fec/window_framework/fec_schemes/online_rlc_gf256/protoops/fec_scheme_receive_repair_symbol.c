
#include <picoquic.h>
#include "../../../../fec.h"
#include "../headers/online_gf256_fec_scheme.h"
#include "../../prng/tinymt32.c"
#include "../gf256/gf256.h"
#include "../../../framework_receiver.h"

/**
 *  fec_scheme_receive_repair_symbol(picoquic_cnx_t *cnx, online_gf256_fec_scheme_t *fec_scheme, window_repair_symbol_t *rs)
 */
protoop_arg_t receive_repair_symbol(picoquic_cnx_t *cnx) {
    plugin_state_t *state = get_plugin_state(cnx);
    if (!state) {
        return (protoop_arg_t) NULL;
    }
    window_fec_framework_receiver_t *wff = (window_fec_framework_receiver_t *) state->framework_receiver;
    online_gf256_fec_scheme_t *fec_scheme = (online_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    window_repair_symbol_t *rs = (window_repair_symbol_t *) get_cnx(cnx, AK_CNX_INPUT, 1);

    equation_t *eq = equation_alloc_with_given_data(cnx, rs);

    // TODO: coefs
    tinymt32_t *prng = my_malloc(cnx, sizeof(tinymt32_t));
    if (!prng) {
        return -1;
    }
    prng->mat1 = 0x8f7011ee;
    prng->mat2 = 0xfc78ff1f;
    prng->tmat = 0x3793fdff;

    get_coefs(cnx, prng, decode_u32(rs->metadata.fss.val), rs->metadata.n_protected_symbols, eq->coefs);

//    window_source_symbol_id_t first_id = MIN(fec_scheme->wrapper.system->first_id_id, rs->metadata.first_id);
//    window_source_symbol_id_t last_id = MAX(fec_scheme->wrapper.system->last_symbol_id, repair_symbol_last_id(rs));


    my_free(cnx, prng);
    equation_t *removed = NULL;
    int used_in_system = 0;
    int ret = wrapper_receive_repair_symbol(cnx, &fec_scheme->wrapper, eq, wff->received_source_symbols, &removed, &used_in_system);
    if (!used_in_system) {
        equation_free_keep_repair_payload(cnx, eq);
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) removed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) used_in_system);

    return ret;
}