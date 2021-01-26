#include <picoquic.h>
#include <getset.h>
#include <zlib.h>
#include "../gf256/swif_symbol.h"
#include "../../prng/tinymt32.c"
#include "../../../types.h"
#include "../headers/online_gf256_fec_scheme.h"


static inline void get_coefs(picoquic_cnx_t *cnx, tinymt32_t *prng, uint32_t seed, int n, uint8_t coefs[n]) {
    tinymt32_init(prng, seed);
    int i;
    for (i = 0 ; i < n ; i++) {
        coefs[i] = (uint8_t) tinymt32_generate_uint32(prng);
        if (coefs[i] == 0)
            coefs[i] = 1;
    }
}

/**
 *
 *
 * Output: return code (int)
 */
protoop_arg_t get_one_coded_symbol(picoquic_cnx_t *cnx)
{

    online_gf256_fec_scheme_t *fs = (online_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    source_symbol_t **source_symbols = (source_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t n_source_symbols = (uint16_t ) get_cnx(cnx, AK_CNX_INPUT, 2);
    window_repair_symbol_t **repair_symbols = (window_repair_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint16_t n_symbols_to_generate = (uint16_t ) get_cnx(cnx, AK_CNX_INPUT, 4);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 5);
    window_source_symbol_id_t first_protected_id = (window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 6);





    tinymt32_t prng;
    prng.mat1 = 0x8f7011ee;
    prng.mat2 = 0xfc78ff1f;
    prng.tmat = 0x3793fdff;
    uint8_t **mul = fs->wrapper.mul_table;
    if (n_source_symbols < 1) {
        PROTOOP_PRINTF(cnx, "IMPOSSIBLE TO GENERATE\n");
        return 1;
    }


    uint8_t *coefs = my_malloc(cnx, align(n_source_symbols*sizeof(uint8_t)));

    uint32_t first_seed = fs->current_repair_symbol;
    for (int i = 0 ; i < n_symbols_to_generate ; i++) {
        uint32_t seed = fs->current_repair_symbol++;

        // generate one symbol
        get_coefs(cnx, &prng, seed, n_source_symbols, coefs);
        window_repair_symbol_t *rs = create_window_repair_symbol(cnx, symbol_size);
        if (!rs)
            return PICOQUIC_ERROR_MEMORY;
        for (int j = 0 ; j < n_source_symbols ; j++) {
            symbol_add_scaled(rs->repair_symbol.repair_payload, coefs[j], source_symbols[j]->_whole_data, align(symbol_size), mul);
        }
        rs->metadata.n_protected_symbols = n_source_symbols;
        rs->metadata.first_id = first_protected_id;
        rs->repair_symbol.payload_length = symbol_size;
        encode_u32(seed, rs->metadata.fss.val);
        repair_symbols[i] = rs;
    }
    // done

    my_free(cnx, coefs);
    // the fec-scheme specific is network-byte ordered
    encode_u32(first_seed, (uint8_t *) &first_seed);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, first_seed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, n_symbols_to_generate);
    return 0;
}
