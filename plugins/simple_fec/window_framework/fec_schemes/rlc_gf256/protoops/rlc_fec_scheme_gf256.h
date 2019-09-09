#include <stdint.h>

typedef struct {
    uint8_t **table_mul;
    uint8_t *table_inv;
    uint32_t current_repair_symbol;
} rlc_gf256_fec_scheme_t;
