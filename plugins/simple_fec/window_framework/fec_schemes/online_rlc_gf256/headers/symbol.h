
#ifndef ONLINE_GAUSSIAN_SYMBOL_H
#define ONLINE_GAUSSIAN_SYMBOL_H

#include <stdint.h>
#include <stddef.h>
#include "../../../types.h"

#define SYMBOL_ID_NONE 0xfffffffful

typedef uint8_t coef_t;
//typedef uint32_t source_symbol_id_t;


//typedef struct {
//    source_symbol_id_t first_id;
//    uint32_t n_protected_symbols;
//    size_t data_length;
//    uint8_t *data;  // the coded data of the repair symbol
//} repair_symbol_t;

static __attribute__((always_inline)) source_symbol_id_t repair_symbol_last_id(window_repair_symbol_t *rs) {
    return (rs->metadata.n_protected_symbols == 0) ? SYMBOL_ID_NONE : (rs->metadata.first_id + rs->metadata.n_protected_symbols - 1);
}

#endif //ONLINE_GAUSSIAN_SYMBOL_H
