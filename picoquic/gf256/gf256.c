/**
 * SWiF Codec: an open-source sliding window FEC codec in C
 * https://github.com/irtf-nwcrg/swif-codec
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "gf256.h"

#define SYMBOL_FAST_MODE true
#define SYMBOL_USE_ALT_LIBRARY true


bool initialized = false;
struct moepgf gflib;

void picoquic_gf256_init() {
    if (!initialized) {
        moepgf_init(&gflib, MOEPGF256,
//                MOEPGF_ALGORITHM_BEST);
                    MOEPGF_ALGORITHM_BEST);
        initialized = true;
    }
}


uint8_t gf256_mul_formula(uint8_t a, uint8_t b)
{
    uint8_t p = 0;
    for (int i = 0 ; i < 8 ; i++) {
        if ((b % 2) == 1) p ^= a;
        b >>= 1;
        bool carry = (a & 0x80) != 0;
        a <<= 1;
        if (carry) {
            a ^= 0x1d;
        }
    }
    return p;
}


/**
 * @brief Take a symbol and add another symbol multiplied by a
 *        coefficient, e.g. performs the equivalent of: p1 += coef * p2
 * @param[in,out] p1     First symbol (to which coef*p2 will be added)
 * @param[in]     coef  Coefficient by which the second packet is multiplied
 * @param[in]     p2     Second symbol
 */
void picoquic_gf256_symbol_add_scaled
        (void *symbol1, uint8_t coef, void *symbol2, uint32_t symbol_size, uint8_t **mul)
{

    if (SYMBOL_USE_ALT_LIBRARY) {
        gflib.maddrc(symbol1, symbol2, coef, symbol_size);
//        galois_w08_region_multiply((char *) symbol2, coef, symbol_size, symbol1, 1);
    } else {
        uint8_t *data1 = (uint8_t *) symbol1;
        uint8_t *data2 = (uint8_t *) symbol2;
        for (uint32_t i=0; i<symbol_size; i++) {
            data1[i] ^= gf256_mul(coef, data2[i], mul);
        }

    }
}

void symbol_add_slow(void *symbol1, void *symbol2, uint32_t symbol_size) {
    uint8_t *data1 = (uint8_t *) symbol1;
    uint8_t *data2 = (uint8_t *) symbol2;
    for (uint32_t i=0; i<symbol_size; i++) {
        data1[i] ^= data2[i];
    }
}
void symbol_add_fast(void *symbol1, void *symbol2, uint32_t symbol_size) {
    uint8_t *data1 = (uint8_t *) symbol1;
    uint8_t *data2 = (uint8_t *) symbol2;
    uint64_t *data64_1 = (uint64_t *) symbol1;
    uint64_t *data64_2 = (uint64_t *) symbol2;
    uint32_t max_64_idx = symbol_size / sizeof(uint64_t);
    for (uint32_t i = 0; i < max_64_idx; i++) {
        data64_1[i] ^= data64_2[i];
    }

    for (uint32_t i = max_64_idx*sizeof(uint64_t) ; i < symbol_size ; i++) {
        data1[i] ^= data2[i];
    }
}

void picoquic_gf256_symbol_add
        (void *symbol1, void *symbol2, uint32_t symbol_size) {

    if (SYMBOL_USE_ALT_LIBRARY) {
        gflib.maddrc(symbol1, symbol2, 1, symbol_size);

//        galois_w08_region_multiply((char *) symbol2, coef, symbol_size, symbol1, 1);
    } else {

        if (SYMBOL_FAST_MODE) {
            symbol_add_fast(symbol1, symbol2, symbol_size);
        } else {
            symbol_add_slow(symbol1, symbol2, symbol_size);
        }
    }
}

bool picoquic_gf256_symbol_is_zero(void *symbol, uint32_t symbol_size) {
    uint8_t *data8 = (uint8_t *) symbol;
    uint64_t *data64 = (uint64_t *) symbol;
    for (uint32_t i = 0 ; i < symbol_size/8 ; i++) {
        if (data64[i] != 0) return false;
    }
    for (uint32_t i = (symbol_size/8)*8 ; i < symbol_size ; i++) {
        if (data8[i] != 0) return false;
    }
    return true;
}



void _symbol_mul
        (uint8_t *symbol1, uint8_t coef, uint32_t symbol_size, uint8_t **mul)
{
//    if (SYMBOL_FAST_MODE) {
//        uint64_t *symbol64 = (uint64_t *) symbol1;
//        for (int i = 0 ; i < symbol_size/sizeof(uint64_t) ; i++) {
//            symbol64[i] =
//        }
//    }
    if (SYMBOL_USE_ALT_LIBRARY) {
        gflib.mulrc(symbol1, coef, symbol_size);

//        galois_w08_region_multiply((char *) symbol2, coef, symbol_size, symbol1, 1);
    } else {

        for (uint32_t i=0; i<symbol_size; i++) {
            symbol1[i] = gf256_mul(coef, symbol1[i], mul);
        }
    }
}



void _symbol_mul_alt
        (uint8_t *symbol1, uint8_t coef, uint32_t symbol_size, uint8_t **mul)
{
//    galois_w08_region_multiply((char *) symbol1, coef, symbol_size, NULL, 0);

    gflib.mulrc(symbol1, coef, symbol_size);
}


void picoquic_gf256_symbol_mul
        (uint8_t *symbol1, uint8_t coef, uint32_t symbol_size, uint8_t **mul)
{
    if (SYMBOL_USE_ALT_LIBRARY) {
        _symbol_mul_alt(symbol1, coef, symbol_size, mul);
    } else {
        _symbol_mul(symbol1, coef, symbol_size, mul);
    }
}
#ifdef __cplusplus
}
#endif

