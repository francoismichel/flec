#include <picoquic.h>
#include "../../helpers.h"
#include "../fec.h"
#include "../prng/tinymt32.c"
#include "../gf256/swif_symbol.c"
#include "rlc_fec_scheme_gf256.h"
#define MIN(a, b) ((a < b) ? a : b)



static __attribute__((always_inline)) void swap(uint8_t **a, int i, int j) {
    uint8_t *tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
}

// returns 1 if a has its first non-zero term before b
// returns -1 if a has its first non-zero term after b
// returns 0 otherwise
static __attribute__((always_inline)) int cmp_eq(uint8_t *a, uint8_t *b, int idx, int n_unknowns) {
    if (a[idx] < b[idx]) return -1;
    else if (a[idx] > b[idx]) return 1;
    else if (a[idx] != 0) return 0;
    return 0;
}

// pre: there is no column full of zeroes
static __attribute__((always_inline)) void sort_system(picoquic_cnx_t *cnx, uint8_t **a, uint8_t **constant_terms, int n_eq, int n_unknowns) {
    // simple selection sort, because there should not be that much equations
    for (int i = 0 ; i < n_eq ; i++) {
        int max = i;
        for (int j = i+1 ; j < n_eq ; j++) {
            if (cmp_eq(a[max], a[j], i, n_unknowns) < 0) {
                max = j;
            }
        }
        swap(a, i, max);
        swap(constant_terms, i, max);
    }
}



/*******
Function that performs Gauss-Elimination and returns the Upper triangular matrix:
There are two options to do this in C.
1. Pass a matrix (a) as the parameter, and calculate and store the upperTriangular(Gauss-Eliminated Matrix) in it.
2. Use malloc and make the function of pointer type and return the pointer.
This program uses the first option.
********/
static __attribute__((always_inline)) void gaussElimination(picoquic_cnx_t *cnx, int n_eq, int n_unknowns, uint8_t **a, uint8_t *constant_terms[n_eq], uint8_t *x[n_eq], bool undetermined[n_unknowns], uint32_t symbol_size, uint8_t **mul, uint8_t *inv){
    PROTOOP_PRINTF(cnx, "N_EQ = %d, n_unknowns = %d a = %p, ct = %p, x = %p\n", n_eq, n_unknowns, (protoop_arg_t) a, (protoop_arg_t) constant_terms, (protoop_arg_t) x);
    sort_system(cnx, a, constant_terms, n_eq, n_unknowns);
    int i,j,k;
    for(i=0;i<n_eq-1;i++){
        for(k=i+1;k<n_eq;k++){
            if(k > i){
                PROTOOP_PRINTF(cnx, "a[%d] = %p, a[%p] = %p\n", i, (protoop_arg_t) a[i], k, (protoop_arg_t) a[k]);
                uint8_t mulnum = a[k][i];
                uint8_t muldenom = a[i][i];
                // term=a[k][i]/a[i][i]
                PROTOOP_PRINTF(cnx, "BEFORE MUL\n");
                uint8_t term = gf256_mul(mulnum, inv[muldenom], mul);
                for(j=0;j<n_unknowns;j++){
                    // a[k][j] -= a[k][i]/a[i][i]*a[i][j]
//                    // i < m-1 AND m <= n, -> i < n-1
                    PROTOOP_PRINTF(cnx, "BEFORE SUB \n");
                      a[k][j] = gf256_sub(a[k][j], gf256_mul(term, a[i][j], mul));
                }
                // a[k][j] -= a[k][i]/a[i][i]*a[i][j] for the big, constant term
                PROTOOP_PRINTF(cnx, "BEFORE SUB_scaled, ct[%d] = %p, ct[%d] = %p\n", k, (protoop_arg_t) constant_terms[k], i, (protoop_arg_t) constant_terms[i]);
                symbol_sub_scaled(constant_terms[k], term, constant_terms[i], symbol_size, mul);
            }
        }
    }
    for (int i = 0 ; i < n_eq ; i++) {
        PROTOOP_PRINTF(cnx, "BEGIN EQ %d\n", i);
        for (int j = 0 ; j < n_unknowns ; j++) {
            PROTOOP_PRINTF(cnx, "%d\n", a[i][j]);
        }
        PROTOOP_PRINTF(cnx, "END EQ\n");
    }
    int candidate = n_unknowns - 1;
    //Begin Back-substitution
    for(i=n_eq-1;i>=0;i--){
        while(a[i][candidate] == 0 && candidate >= 0) {
            undetermined[candidate--] = true;
        }
        PROTOOP_PRINTF(cnx, "BEFORE MEMCPY, CANDIDATE = %d, i = %d, SIZE = %d\n", candidate, i, symbol_size);
        my_memcpy(x[candidate], constant_terms[i], symbol_size);
        for (int j = 0 ; j < candidate ; j++) {
            if (a[i][j] != 0) {
                // if this variable depends on another one with a smaller index, it is undefined, as we don't know the value of the one with a smaller index
                undetermined[candidate] = true;
                break;
            }
        }
        for(j=candidate+1;j<n_unknowns;j++){
//             x[i]=x[i]-a[i][j]*x[j];
            if (a[i][j] != 0) {
                if (undetermined[j]) {
                    // if the unknown depends on an undetermined unknown, this unknown is undetermined
                    undetermined[candidate] = true;
                } else {
                    symbol_sub_scaled(x[candidate], a[i][j], x[j], symbol_size, mul);
                    a[i][j] = 0;
                }
            }
        }
        // i < n_eq <= n_unknowns, so a[i][i] is small
        if (symbol_is_zero(x[candidate], symbol_size) || a[i][candidate] == 0) {
            // this solution is undetermined
            undetermined[candidate] = true;
            PROTOOP_PRINTF(cnx, "UNDETERMINED SOL\n");
            // TODO
        } else if (!undetermined[candidate]) {
            // x[i] = x[i]/a[i][i]
            symbol_mul(x[candidate], inv[a[i][candidate]], symbol_size, mul);
            a[i][candidate] = gf256_mul(a[i][candidate], inv[a[i][candidate]], mul);
        }
        candidate--;
    }
    // it marks all the variables with an index <= candidate as undetermined
    // we use a my_memset although it is harder to understand because with a for loop, the compiler will translate it into a call to memset
    if (candidate >= 0) {
        my_memset(undetermined, true, (candidate+1)*sizeof(bool));
    }
}

static __attribute__((always_inline)) void get_coefs(picoquic_cnx_t *cnx, tinymt32_t *prng, uint32_t seed, int n, uint8_t *coefs) {
    tinymt32_init(prng, seed);
    int i;
    for (i = 0 ; i < n ; i++) {
        coefs[i] = (uint8_t) tinymt32_generate_uint32(prng);
        if (coefs[i] == 0)
            coefs[i] = 1;
    }
}

// TODO: handle when malloc returns NULL

/**
 * fec_block_t* fec_block = (fec_block_t *) cnx->protoop_inputv[0];
 *
 * Output: return code (int)
 */
protoop_arg_t fec_recover(picoquic_cnx_t *cnx)
{
    fec_block_t *fec_block = (fec_block_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    rlc_gf256_fec_scheme_t *fs = (rlc_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    // FIXME: we assume that the id of the smallest protected source symbol (received or not) is encoded in the fec block number
    uint32_t smallest_protected = fec_block->fec_block_number;
    uint8_t **mul = fs->table_mul;
    PROTOOP_PRINTF(cnx, "TRYING TO RECOVER SYMBOLS WITH RLC256 FOR BLOCK %u !\n", fec_block->fec_block_number);
    if (fec_block->total_repair_symbols == 0 || fec_block->current_source_symbols == fec_block->total_source_symbols ||
        fec_block->current_source_symbols + fec_block->current_repair_symbols < fec_block->total_source_symbols) {
        PROTOOP_PRINTF(cnx, "NO RECOVERY TO DO\n");
        return 0;
    }
    tinymt32_t *prng = my_malloc(cnx, sizeof(tinymt32_t));
    prng->mat1 = 0x8f7011ee;
    prng->mat2 = 0xfc78ff1f;
    prng->tmat = 0x3793fdff;

    PROTOOP_PRINTF(cnx, "RECOVERING\n");
    int n_unknowns = fec_block->total_source_symbols - fec_block->current_source_symbols;
    int n_eq = MIN(n_unknowns, fec_block->current_repair_symbols);
    int i = 0;
    uint8_t *coefs = my_malloc(cnx, fec_block->total_source_symbols*sizeof(uint8_t));//[fec_block->total_source_symbols];
    uint8_t **unknowns = my_malloc(cnx, (n_unknowns)*sizeof(uint8_t *));;//[n_unknowns];
    uint8_t **system_coefs = my_malloc(cnx, n_eq*sizeof(uint8_t *));//[n_eq][n_unknowns + 1];
    uint8_t **constant_terms = my_malloc(cnx, n_eq*sizeof(uint8_t *));
    bool *undetermined = my_malloc(cnx, n_unknowns*sizeof(bool));
    my_memset(undetermined, 0, n_unknowns*sizeof(bool));



    if (!coefs || !unknowns || !system_coefs) {
        PROTOOP_PRINTF(cnx, "NOT ENOUGH MEM\n");
    }

    for (int j = 0 ; j < n_eq ; j++) {
        system_coefs[j] = my_malloc(cnx, (n_unknowns) * sizeof(uint8_t));
        if (!system_coefs[j]) {
            PROTOOP_PRINTF(cnx, "NOT ENOUGH MEM\n");
        }
    }


    uint16_t max_length = 0;
    repair_symbol_t *rs;
    for_each_repair_symbol(fec_block, rs) {
        if (rs) {
            max_length = MAX(max_length, rs->data_length);
        }
    }
    for (int j = 0 ; j < n_unknowns ; j++) {
        unknowns[j] = my_malloc(cnx, max_length);
        my_memset(unknowns[j], 0, max_length);
    }

    for (int i = 0 ; i < fec_block->total_source_symbols ; i++) {
        if (!fec_block->source_symbols[i])
            PROTOOP_PRINTF(cnx, "%u IS LOST\n", smallest_protected + i);
    }

    // building the system, equation by equation
    i = 0;
    for_each_repair_symbol(fec_block, rs) {
//        PROTOOP_PRINTF(cnx, "I = %d\n", i);
        if (rs && i < n_eq) {
            uint32_t smallest_protected_by_rs = rs->repair_fec_payload_id.source_fpid.raw;
            PROTOOP_PRINTF(cnx, "TRY RS, [%u, ...]\n", smallest_protected_by_rs);
            bool protects_at_least_one_source_symbol = false;
            for (int k = smallest_protected_by_rs - smallest_protected ; k < smallest_protected_by_rs + rs->nss - smallest_protected ; k++) {
                // this source symbol is protected by this repair symbol
                if (!fec_block->source_symbols[k]) {
                    protects_at_least_one_source_symbol = true;
                    PROTOOP_PRINTF(cnx, "PROTECTS AT LEAST SS %d\n", smallest_protected + k);
                    break;
                }
            }
            if (protects_at_least_one_source_symbol) {
                PROTOOP_PRINTF(cnx, "ADD CONSTANT TERM %d\n", i);
                constant_terms[i] = my_malloc(cnx, max_length);
                if (!constant_terms[i]) {
                    PROTOOP_PRINTF(cnx, "COULD NOT ALLOCATE CONSTANT TERM\n");
//                    return -1
                }
                my_memset(constant_terms[i], 0, max_length);
                my_memcpy(constant_terms[i], rs->data, rs->data_length);
                PROTOOP_PRINTF(cnx, "BEFORE MEMSET 2\n");
                my_memset(system_coefs[i], 0, fec_block->total_source_symbols);
//                PROTOOP_PRINTF(cnx, "BEFORE GET COEFS, SMALLEST BY RS = %u, SMALLEST = %u\n", smallest_protected_by_rs, smallest_protected);
                get_coefs(cnx, prng, (rs->repair_fec_payload_id.fec_scheme_specific), rs->nss, &coefs[smallest_protected_by_rs - smallest_protected]);
                PROTOOP_PRINTF(cnx, "AFTER GET COEFS\n");
                int current_unknown = 0;
                for (int j = smallest_protected_by_rs - smallest_protected ; j < smallest_protected_by_rs + rs->nss - smallest_protected ; j++) {
                    // this source symbol is protected by this repair symbol
                    if (fec_block->source_symbols[j]) {
                        // we add data_length to avoid overflowing on the source symbol. As we assume the source symbols are padded to 0, there is no harm in not adding the zeroes
                        symbol_sub_scaled(constant_terms[i], coefs[j], fec_block->source_symbols[j]->data, fec_block->source_symbols[j]->data_length, mul);
                    } else if (current_unknown < n_unknowns) {
                        system_coefs[i][current_unknown++] = coefs[j];
                    }
                }
                i++;
            }
        }
    }

    int n_effective_equations = i;

    PROTOOP_PRINTF(cnx, "BEFORE GAUSSIAN, LENGTH = %d\n", max_length);
    // the system is built: let's recover it
    bool can_recover = n_effective_equations >= n_unknowns;
    if (can_recover)
        gaussElimination(cnx, n_effective_equations, n_unknowns, system_coefs, constant_terms, unknowns, undetermined, max_length, mul, fs->table_inv);
    else
        PROTOOP_PRINTF(cnx, "CANNOT RECOVER\n");
    PROTOOP_PRINTF(cnx, "AFTER GAUSSIAN\n");
    int current_unknown = 0;
    for (int j = 0 ; j < fec_block->total_source_symbols ; j++) {
        if (!fec_block->source_symbols[j] && can_recover && !undetermined[current_unknown] && !symbol_is_zero(unknowns[current_unknown], max_length)) {
            // TODO: handle the case where source symbols could be 0
            source_symbol_t *ss = malloc_source_symbol(cnx, (source_fpid_t) (((fec_block->fec_block_number) << 8) + ((uint8_t)j)), max_length);
            if (!ss) {
                my_free(cnx, unknowns[current_unknown++]);
                continue;
            }
            ss->source_fec_payload_id.raw = fec_block->fec_block_number + j;
            my_memcpy(ss->data, unknowns[current_unknown], max_length);
            ss->data_length = max_length;
            fec_block->source_symbols[j] = ss;
            fec_block->current_source_symbols++;
            my_free(cnx, unknowns[current_unknown++]);
        } else if (!fec_block->source_symbols[j] && (!can_recover || undetermined[current_unknown] || symbol_is_zero(unknowns[current_unknown], max_length))) {
            // this unknown could not be recovered
            my_free(cnx, unknowns[current_unknown++]);
        }
    }

    // free the system
    for (i = 0 ; i < n_eq ; i++) {
        my_free(cnx, system_coefs[i]);
        if (i < n_effective_equations)
            my_free(cnx, constant_terms[i]);
    }
    my_free(cnx, prng);
    my_free(cnx, system_coefs);
    my_free(cnx, constant_terms);
    my_free(cnx, unknowns);
    my_free(cnx, coefs);
    my_free(cnx, undetermined);

    return 0;
}