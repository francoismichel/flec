#include "picoquic.h"

#define DEFAULT_N 30
#define DEFAULT_K 25
// the sliding mean must be in the form 1/ALPHA_DENOMINATOR, because we don't have floats in ebpf and we would like to avoid fractions
#define ALPHA_DENOMINATOR ((uint64_t) 1000) // if ALPHA_DENOMINATOR == 1000, alpha = 0.001
#define GRANULARITY ((uint64_t) 1000000000)

typedef struct {
    uint64_t loss_rate_times_granularity;
} dynamic_uniform_redundancy_controller_t;