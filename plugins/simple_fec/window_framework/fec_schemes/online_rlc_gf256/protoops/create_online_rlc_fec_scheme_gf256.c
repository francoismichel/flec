#include <picoquic.h>
#include <memcpy.h>
#include "../gf256/generated_table_code.c"
#include "../../../../../helpers.h"
#include "../headers/online_gf256_fec_scheme.h"


static __attribute__((always_inline)) int create_fec_schemes(picoquic_cnx_t *cnx, online_gf256_fec_scheme_t *fec_schemes[2]) {
    // TODO: free when error
    online_gf256_fec_scheme_t *fs = my_malloc_ex(cnx, sizeof(online_gf256_fec_scheme_t));
    if (!fs)
        return PICOQUIC_ERROR_MEMORY;
    system_t *system = system_alloc(cnx);
    if (!system)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t **table_mul = my_malloc_ex(cnx, 256*sizeof(uint8_t *));
    if (!table_mul)
        return PICOQUIC_ERROR_MEMORY;
    uint8_t *table_inv = my_malloc_ex(cnx, 256*sizeof(uint8_t));
    if (!table_inv)
        return PICOQUIC_ERROR_MEMORY;
    my_memset(table_inv, 0, 256*sizeof(uint8_t));
    assign_inv(table_inv);
    PROTOOP_PRINTF(cnx, "GENERATE MUL\n");
    for (int i = 0 ; i < 256 ; i++) {
        table_mul[i] = my_malloc_ex(cnx, 256 * sizeof(uint8_t));
        if (!table_mul[i])
            return PICOQUIC_ERROR_MEMORY;
        my_memset(table_mul[i], 0, 256*sizeof(uint8_t));
    }
    assign_mul(table_mul);
    picoquic_gf256_init();
    wrapper_init(cnx, &fs->wrapper, system, table_inv, table_mul);
    fs->current_repair_symbol = 0;
    fec_schemes[0] = fs;
    fec_schemes[1] = fs;
    return 0;
}



protoop_arg_t create_fec_scheme(picoquic_cnx_t *cnx)
{
    online_gf256_fec_scheme_t *fs[2];
    int ret = create_fec_schemes(cnx, fs);
    if (ret) {
        PROTOOP_PRINTF(cnx, "ERROR CREATING GF256\n");
        return ret;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) fs[0]);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) fs[1]);
    return 0;
}
