/**
 * SWiF Codec: an open-source sliding window FEC codec in C
 * https://github.com/irtf-nwcrg/swif-codec
 */

#ifndef __PICOQUIC_SYMBOL_H__
#define __PICOQUIC_SYMBOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SYMBOL_FAST_MODE true
#ifndef SYMBOL_USE_ALT_LIBRARY
#define SYMBOL_USE_ALT_LIBRARY false
#endif

#if SYMBOL_USE_ALT_LIBRARY == true

#include "moepgf/moepgf.h"
#endif

extern struct moepgf gflib;

void picoquic_gf256_init();

static __attribute__((always_inline)) uint8_t gf256_mul(uint8_t a, uint8_t b, uint8_t **mul)
{ return mul[a][b]; }

/**
 * @brief Take a symbol and add another symbol multiplied by a
 *        coefficient, e.g. performs the equivalent of: p1 += coef * p2
 * @param[in,out] p1     First symbol (to which coef*p2 will be added)
 * @param[in]     coef  Coefficient by which the second packet is multiplied
 * @param[in]     p2     Second symbol
 */
void picoquic_gf256_symbol_add_scaled
        (void *symbol1, uint8_t coef, void *symbol2, uint32_t symbol_size, uint8_t **mul);

void picoquic_gf256_symbol_add
        (void *symbol1, void *symbol2, uint32_t symbol_size);

bool picoquic_gf256_symbol_is_zero(void *symbol, uint32_t symbol_size);

void picoquic_gf256_symbol_mul
        (uint8_t *symbol1, uint8_t coef, uint32_t symbol_size, uint8_t **mul);
#ifdef __cplusplus
}
#endif

#endif /* __PICOQUIC_SYMBOL_H__ */
