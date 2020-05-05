/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
#ifndef SWIF_SYMBOL_H
#define SWIF_SYMBOL_H
#define symbol_sub_scaled symbol_add_scaled
#define gf256_add(a, b) (a^b)
#define gf256_sub gf256_add
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "/home/michelfra/.local/include/moepgf/moepgf.h"

#define SYMBOL_FAST_MODE true
#define SYMBOL_USE_ALT_LIBRARY true

//typedef void	(*maddrc_t)	(uint8_t *, const uint8_t *, uint8_t, size_t);
//typedef void	(*mulrc_t)	(uint8_t *, uint8_t, size_t);

struct moepgf gflib;

void init_lib() {
    moepgf_init(&gflib, MOEPGF256,
//                MOEPGF_ALGORITHM_BEST);
                MOEPGF_ALGORITHM_BEST);
}
/*---------------------------------------------------------------------------*/
#endif