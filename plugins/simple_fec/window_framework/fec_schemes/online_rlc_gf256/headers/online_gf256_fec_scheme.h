
#ifndef PICOQUIC_ONLINE_GF256_FEC_SCHEME_H
#define PICOQUIC_ONLINE_GF256_FEC_SCHEME_H

#include "system_wrapper.h"

typedef struct {
    system_wrapper_t wrapper;
    uint32_t current_repair_symbol;
} online_gf256_fec_scheme_t;

#endif //PICOQUIC_ONLINE_GF256_FEC_SCHEME_H
