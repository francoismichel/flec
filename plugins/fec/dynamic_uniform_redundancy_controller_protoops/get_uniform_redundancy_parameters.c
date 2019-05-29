#include "picoquic.h"
#include "../bpf.h"
#include "dynamic_uniform_redundancy_controller.h"

// sets as output:
// Input  0: the redundancy controller state
// Output 0: the size of a block
// Output 1: the numTravaillerber of source symbols in a block
protoop_arg_t get_constant_redundancy_parameters(picoquic_cnx_t *cnx)
{
    dynamic_uniform_redundancy_controller_t *urc = (dynamic_uniform_redundancy_controller_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint8_t n = DEFAULT_N;
    uint8_t k = DEFAULT_K;
    // n = 1/loss_rate
    // k = n-1
    uint64_t receive_rate_times_granularity = GRANULARITY - urc->loss_rate_times_granularity;
    // we don't want a better perecision than in percents because we don't want a window of more than 100 packets
    uint64_t receive_rate_in_percents = (receive_rate_times_granularity*100)/GRANULARITY;
    if (receive_rate_in_percents > 0) {
        n = MAX(3, MIN(MAX_SYMBOLS_PER_FEC_BLOCK, receive_rate_in_percents));
        k = n-1;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, n);
    // as we assume the loss rate to be uniform there is no point in sending bursts of repair symbols, so send only one repair symbol
    set_cnx(cnx, AK_CNX_OUTPUT, 1, k);
    PROTOOP_PRINTF(cnx, "RETURN DYNAMIC UNIFORM PARAMETERS N = %u, K = %u, ALPHA_DENOMINATOR = %lu, LOSS_RATE_TIMES_GRANULARITY = %lu\n", n, k, ALPHA_DENOMINATOR, urc->loss_rate_times_granularity);
    return 0;
}