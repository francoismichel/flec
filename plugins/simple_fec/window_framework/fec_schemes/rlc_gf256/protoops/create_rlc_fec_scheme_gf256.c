#include <picoquic.h>
#include <memcpy.h>
#include <memory.h>
#include "rlc_fec_scheme_gf256.h"
#include "../gf256/generated_table_code.c"
#include "../../../../../helpers.h"


static __attribute__((always_inline)) int create_fec_schemes(picoquic_cnx_t *cnx, rlc_gf256_fec_scheme_t *fec_schemes[2]) {
    // TODO: free when error
    rlc_gf256_fec_scheme_t *fs = my_malloc_ex(cnx, sizeof(rlc_gf256_fec_scheme_t));
    if (!fs)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t **table_mul = my_malloc_ex(cnx, 256*sizeof(uint8_t *));
    if (!table_mul)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t *table_inv = my_malloc_ex(cnx, 256*sizeof(uint8_t));
    if (!table_inv)
        return PICOQUIC_ERROR_MEMORY;
    my_memset(table_inv, 0, 256*sizeof(uint8_t));
    assign_inv(table_inv);
    for (int i = 0 ; i < 256 ; i++) {
        table_mul[i] = my_malloc_ex(cnx, 256 * sizeof(uint8_t));
        if (!table_mul[i])
            return PICOQUIC_ERROR_MEMORY;
        my_memset(table_mul[i], 0, 256*sizeof(uint8_t));
    }
    assign_mul(table_mul);
    fs->table_mul = table_mul;
    fs->table_inv = table_inv;
    fs->current_repair_symbol = 0;
    uint8_t **mmul = table_mul;
    uint8_t *inv = table_inv;
    fec_schemes[0] = fs;
    fec_schemes[1] = fs;
    return 0;
}



protoop_arg_t create_fec_scheme(picoquic_cnx_t *cnx)
{
    rlc_gf256_fec_scheme_t *fs[2];
    int ret = create_fec_schemes(cnx, fs);
    if (ret) {
        PROTOOP_PRINTF(cnx, "ERROR CREATING GF256\n");
        return ret;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) fs[0]);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) fs[1]);
    return 0;
}
