
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
        PROTOOP_PRINTF(cnx, "RECOVERED %u, INDEX = %d, FIRST SYSTEM ID = %u\n", recovered_id, arraylist_index(&wrapper->unknowns_ids, recovered_id), wrapper->system->first_id_id);
        equation_t *eq = system_get_normalized_pivot_for_id(cnx, wrapper->system, wrapper->system->first_id_id + arraylist_index(&wrapper->unknowns_ids, recovered_id), wrapper->inv_table, wrapper->mul_table);
        PROTOOP_PRINTF(cnx, "GOT EQ, FIRST ID = %u, LAST ID = %u, HAS ONE ID = %u, IS ZERO = %u\n", eq->pivot, eq->last_non_zero_id, equation_has_one_id(eq), equation_is_zero(eq));
        if (eq && equation_has_one_id(eq)) {
            window_source_symbol_t *ss = create_window_source_symbol(cnx, symbol_size);
            if (!ss) {
                return PICOQUIC_ERROR_MEMORY;
            }
            PROTOOP_PRINTF(cnx, "RECOVERED PIVOT = %u, PIVOT COEF = %u\n", eq->pivot, equation_get_coef(eq, eq->pivot));
            PROTOOP_PRINTF(cnx, "eq[0, 1, 2, 3] = %u, %u, %u, %u, %u, %u, %u, %u, %u\n", eq->constant_term.repair_symbol.repair_payload[0], eq->constant_term.repair_symbol.repair_payload[1], eq->constant_term.repair_symbol.repair_payload[2],
                    eq->constant_term.repair_symbol.repair_payload[3], eq->constant_term.repair_symbol.repair_payload[4], eq->constant_term.repair_symbol.repair_payload[5], eq->constant_term.repair_symbol.repair_payload[6], eq->constant_term.repair_symbol.repair_payload[7], eq->constant_term.repair_symbol.repair_payload[8]);
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