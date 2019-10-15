#include <picoquic.h>
#include <getset.h>
#include "../gf256/swif_symbol.c"
#include "../gf256/prng/tinymt32.c"
#include "rlc_fec_scheme_gf256.h"
#include "../../../types.h"


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
 * fec_block_t* fec_block = (fec_block_t *) cnx->protoop_inputv[0];
 *
 * Output: return code (int)
 */
protoop_arg_t get_one_coded_symbol(picoquic_cnx_t *cnx)
{

    rlc_gf256_fec_scheme_t *fs = (rlc_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    window_source_symbol_t **source_symbols = (window_source_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t n_source_symbols = (uint16_t ) get_cnx(cnx, AK_CNX_INPUT, 2);
    window_repair_symbol_t **repair_symbols = (window_repair_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint16_t n_symbols_to_generate = (uint16_t ) get_cnx(cnx, AK_CNX_INPUT, 4);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 5);





    tinymt32_t prng;
    prng.mat1 = 0x8f7011ee;
    prng.mat2 = 0xfc78ff1f;
    prng.tmat = 0x3793fdff;
    uint8_t **mul = fs->table_mul;
    if (n_source_symbols < 1) {
        PROTOOP_PRINTF(cnx, "IMPOSSIBLE TO GENERATE\n");
        return 1;
    }


    uint8_t *coefs = my_malloc(cnx, n_source_symbols*sizeof(uint8_t));
    uint8_t **knowns = my_malloc(cnx, n_source_symbols*sizeof(uint8_t *));

    for (int i = 0 ; i < n_source_symbols ; i++) {
        knowns[i] = my_malloc(cnx, symbol_size);
        my_memset(knowns[i], 0, symbol_size);
        my_memcpy(knowns[i], source_symbols[i]->source_symbol._whole_data, symbol_size);
    }



    uint32_t first_seed = fs->current_repair_symbol;
    for (int i = 0 ; i < n_symbols_to_generate ; i++) {
        uint32_t seed = fs->current_repair_symbol++;

        // generate one symbol
        get_coefs(cnx, &prng, seed, n_source_symbols, coefs);
        window_repair_symbol_t *rs = create_window_repair_symbol(cnx, symbol_size);
        if (!rs)
            return PICOQUIC_ERROR_MEMORY;
        for (int j = 0 ; j < n_source_symbols ; j++) {
            symbol_add_scaled(rs->repair_symbol.repair_payload, coefs[j], knowns[j], symbol_size, mul);
        }
        rs->metadata.n_protected_symbols = n_source_symbols;
        rs->metadata.first_id = source_symbols[0]->id;
        rs->repair_symbol.payload_length = symbol_size;
        encode_u32(seed, rs->metadata.fss.val);
        repair_symbols[i] = rs;

    }
    // done

    for (int i = 0 ; i < n_source_symbols ; i++) {
        my_free(cnx, knowns[i]);
    }
    my_free(cnx, coefs);
    my_free(cnx, knowns);
    // the fec-scheme specific is network-byte ordered
    encode_u32(first_seed, (uint8_t *) &first_seed);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, first_seed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, n_symbols_to_generate);
    return 0;
}
