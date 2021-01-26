
#include <picoquic.h>
#include "../../../../fec.h"
#include "../../../framework_receiver.h"
#include "../headers/online_gf256_fec_scheme.h"
#include "../../prng/tinymt32.c"

/**
 *  fec_scheme_receive_repair_symbol(picoquic_cnx_t *cnx, online_gf256_fec_scheme_t *fec_scheme, window_repair_symbol_t *rs)
 */
protoop_arg_t recovver_lost_symbols(picoquic_cnx_t *cnx) {
    online_gf256_fec_scheme_t *fec_scheme = (online_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    arraylist_t *recovered_symbols = (arraylist_t *) get_cnx(cnx, AK_CNX_INPUT, 2);

    system_wrapper_t *wrapper = &fec_scheme->wrapper;

    protoop_arg_t recovered = 0;
    for (int i = 0 ; i < arraylist_size(&wrapper->recovered_ids_arraylist) ; i++) {
        source_symbol_id_t recovered_id = (source_symbol_id_t) arraylist_get(&wrapper->recovered_ids_arraylist, i);
        equation_t *eq = system_get_normalized_pivot_for_id(cnx, wrapper->system, wrapper->system->first_id_id + arraylist_index(&wrapper->unknowns_ids, recovered_id), wrapper->inv_table, wrapper->mul_table);
        if (eq && equation_has_one_id(eq)) {
            window_source_symbol_t *ss = create_window_source_symbol(cnx, symbol_size);
            if (!ss) {
                return PICOQUIC_ERROR_MEMORY;
            }
            my_memcpy(ss->source_symbol._whole_data, eq->constant_term.repair_symbol.repair_payload, eq->constant_term.repair_symbol.payload_length);
            ss->id = recovered_id;
            arraylist_push(cnx, recovered_symbols, (uintptr_t) ss);
            recovered = 1;
        }
    }
    // now, empty the arraylist
    arraylist_reset(&wrapper->recovered_ids_arraylist);


    set_cnx(cnx, AK_CNX_OUTPUT, 0, recovered);

    return 0;
}