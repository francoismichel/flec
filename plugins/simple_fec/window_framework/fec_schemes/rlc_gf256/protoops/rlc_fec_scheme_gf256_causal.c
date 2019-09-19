#include <picoquic.h>
#include "../gf256/prng/tinymt32.c"
#include "../gf256/swif_symbol.c"
#include "rlc_fec_scheme_gf256.h"
#include "../../../types.h"

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

// shuffles an array of repair symbols
static __attribute__((always_inline)) void shuffle_repair_symbols(picoquic_cnx_t *cnx, window_repair_symbol_t **rss, int size, tinymt32_t *prng) {
    for (int i = 0 ; i < size - 1 ; i++) {
        int j = tinymt32_generate_uint32(prng) % size;
        window_repair_symbol_t *tmp = rss[i];
        rss[i] = rss[j];
        rss[j] = tmp;
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

    sort_system(cnx, a, constant_terms, n_eq, n_unknowns);

    int i,j,k;
    for(i=0;i<n_eq-1;i++){
        for(k=i+1;k<n_eq;k++){
            if(k > i){
                uint8_t mulnum = a[k][i];
                uint8_t muldenom = a[i][i];
                // term=a[k][i]/a[i][i]
                uint8_t term = gf256_mul(mulnum, inv[muldenom], mul);
                for(j=0;j<n_unknowns;j++){
                    // a[k][j] -= a[k][i]/a[i][i]*a[i][j]
//                    // i < m-1 AND m <= n, -> i < n-1
                      a[k][j] = gf256_sub(a[k][j], gf256_mul(term, a[i][j], mul));
                }
                // a[k][j] -= a[k][i]/a[i][i]*a[i][j] for the big, constant term
                symbol_sub_scaled(constant_terms[k], term, constant_terms[i], symbol_size, mul);
            }
        }
    }
    sort_system(cnx, a, constant_terms, n_eq, n_unknowns);
    int candidate = n_unknowns - 1;
    //Begin Back-substitution
    for(i=n_eq-1;i>=0;i--){
        bool only_zeroes = true;
        for (int j = 0 ; j < n_unknowns ; j++) {
            if (a[i][j] != 0) {
                only_zeroes = false;
                break;
            }
        }
        if (!only_zeroes) {
            while(a[i][candidate] == 0 && candidate >= 0) {
                undetermined[candidate--] = true;
            }
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
                // TODO
            } else if (!undetermined[candidate]) {
                // x[i] = x[i]/a[i][i]
                symbol_mul(x[candidate], inv[a[i][candidate]], symbol_size, mul);
                a[i][candidate] = gf256_mul(a[i][candidate], inv[a[i][candidate]], mul);
            }
            candidate--;
        }
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


/**
 * recovers the missing symbols from a window
 * this function assumes that the
 * \param[in] fec_scheme <b> rlc_gf256_fec_scheme_t* </b> the fec scheme state
 * \param[in] source_symbols <b> window_source_symbol_t ** </b> array of source symbols (a symbol is NULL if it is not present)
 * \param[in] n_source_symbols <b> uint16_t </b> size of source_symbols
 * \param[in] repair_symbols <b> window_repair_symbol_t ** </b> array of repair symbols
 * \param[in] n_repair_symbols <b> uint16_t </b> size of repair_symbols
 * \param[in] n_missing_source_symbols <b> uint16_t </b> number of missing source symbols in the array
 * \param[in] smallest_source_symbol_id <b> window_source_symbol_id_t </b> the id of the smallest source symbol in the array
 * \param[in] symbol_size <b> uint16_t </b> size of a source/repair symbol in bytes
 *
 * \return \b int Error code, 0 iff everything was fine
 */
protoop_arg_t fec_recover(picoquic_cnx_t *cnx)
{
    rlc_gf256_fec_scheme_t *fs = (rlc_gf256_fec_scheme_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    window_source_symbol_t **source_symbols = (window_source_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t n_source_symbols = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    window_repair_symbol_t **repair_symbols = (window_repair_symbol_t **) get_cnx(cnx, AK_CNX_INPUT, 3);
    uint16_t n_repair_symbols = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 4);
    uint16_t n_missing_source_symbols = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 5);
    window_source_symbol_id_t smallest_protected = (window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 6);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 7);

    window_repair_symbol_t *rs;




    uint8_t **mul = fs->table_mul;
    if (n_repair_symbols == 0 || n_missing_source_symbols == 0 ||
        n_missing_source_symbols > n_repair_symbols) {
        return 0;
    }
    tinymt32_t *prng = my_malloc(cnx, sizeof(tinymt32_t));
    prng->mat1 = 0x8f7011ee;
    prng->mat2 = 0xfc78ff1f;
    prng->tmat = 0x3793fdff;

    int n_eq = MIN(n_missing_source_symbols, n_repair_symbols);
    int i = 0;
    uint8_t *coefs = my_malloc(cnx, n_source_symbols);
    uint8_t **unknowns = my_malloc(cnx, (n_missing_source_symbols)*sizeof(uint8_t *));
    uint8_t **system_coefs = my_malloc(cnx, n_eq*sizeof(uint8_t *));//[n_eq][n_unknowns + 1];
    uint8_t **constant_terms = my_malloc(cnx, n_eq*sizeof(uint8_t *));
    bool *undetermined = my_malloc(cnx, n_missing_source_symbols*sizeof(bool));
    my_memset(undetermined, 0, n_missing_source_symbols*sizeof(bool));



    if (!coefs || !unknowns || !system_coefs) {
        PROTOOP_PRINTF(cnx, "NOT ENOUGH MEM\n");
        return PICOQUIC_ERROR_MEMORY;
    }

    for (int j = 0 ; j < n_eq ; j++) {
        system_coefs[j] = my_malloc(cnx, (n_missing_source_symbols) * sizeof(uint8_t));
        if (!system_coefs[j]) {
            PROTOOP_PRINTF(cnx, "NOT ENOUGH MEM\n");
            return PICOQUIC_ERROR_MEMORY;
        }
    }

    for (int j = 0 ; j < n_missing_source_symbols ; j++) {
        unknowns[j] = my_malloc(cnx, symbol_size);
        my_memset(unknowns[j], 0, symbol_size);
    }

    // building the system, equation by equation
    bool *protected_symbols = my_malloc(cnx, n_source_symbols*sizeof(bool));
    if (!protected_symbols)
        return PICOQUIC_ERROR_MEMORY;
    my_memset(protected_symbols, 0, n_source_symbols*sizeof(bool));
    i = 0;
    tinymt32_t *shuffle_prng = my_malloc(cnx, sizeof(tinymt32_t));
    shuffle_prng->mat1 = 0x8f7011ee;
    shuffle_prng->mat2 = 0xfc78ff1f;
    shuffle_prng->tmat = 0x3793fdff;
    tinymt32_init(shuffle_prng, picoquic_current_time());
    shuffle_repair_symbols(cnx, repair_symbols, n_repair_symbols, shuffle_prng);
    my_free(cnx, shuffle_prng);
    for_each_window_repair_symbol(repair_symbols, rs, n_repair_symbols) {
        if (rs && i < n_eq) {
            window_source_symbol_id_t smallest_protected_by_rs = rs->metadata.first_id;
            bool protects_at_least_one_new_source_symbol = false;
            for (int k = smallest_protected_by_rs - smallest_protected ; k < smallest_protected_by_rs + rs->metadata.n_protected_symbols - smallest_protected ; k++) {
                // this source symbol is protected by this repair symbol
                if (!source_symbols[k] && !protected_symbols[k]) {
                    protects_at_least_one_new_source_symbol = true;
                    protected_symbols[k] = true;
                    break;
                }
            }
            if (protects_at_least_one_new_source_symbol) {
                constant_terms[i] = my_malloc(cnx, symbol_size);
                if (!constant_terms[i]) {
                    return -1;
                }
                my_memset(constant_terms[i], 0, symbol_size);
                my_memcpy(constant_terms[i], rs->repair_symbol.repair_payload, symbol_size);
                my_memset(system_coefs[i], 0, n_source_symbols);
                get_coefs(cnx, prng, decode_u32(rs->metadata.fss.val), rs->metadata.n_protected_symbols, &coefs[smallest_protected_by_rs - smallest_protected]);
                int current_unknown = 0;
                for (int j = smallest_protected_by_rs - smallest_protected ; j < smallest_protected_by_rs + rs->metadata.n_protected_symbols - smallest_protected ; j++) {
                    // this source symbol is protected by this repair symbol
                    if (source_symbols[j]) {
                        // we add data_length to avoid overflowing on the source symbol. As we assume the source symbols are padded to 0, there is no harm in not adding the zeroes
                        symbol_sub_scaled(constant_terms[i], coefs[j], source_symbols[j]->source_symbol._whole_data, symbol_size, mul);
                    } else if (current_unknown < n_missing_source_symbols) {
                        system_coefs[i][current_unknown++] = coefs[j];
                    }
                }
                i++;
            }
        }
    }
    my_free(cnx, protected_symbols);
    int n_effective_equations = i;

    // the system is built: let's recover it
    bool can_recover = n_effective_equations >= n_missing_source_symbols;
    if (can_recover)
        gaussElimination(cnx, n_effective_equations, n_missing_source_symbols, system_coefs, constant_terms, unknowns, undetermined, symbol_size, mul, fs->table_inv);
    else
        PROTOOP_PRINTF(cnx, "CANNOT RECOVER, %d EQUATIONS, %d MISSING SYMBOLS\n", n_effective_equations, n_missing_source_symbols);
    int current_unknown = 0;
    int err = 0;
    for (int j = 0 ; j < n_source_symbols ; j++) {
        if (!source_symbols[j] && can_recover && !undetermined[current_unknown] && !symbol_is_zero(unknowns[current_unknown], symbol_size)) {
            // TODO: handle the case where source symbols could be 0
            window_source_symbol_t *ss = create_window_source_symbol(cnx, symbol_size);
            if (!ss) {
                my_free(cnx, unknowns[current_unknown++]);
                err = PICOQUIC_ERROR_MEMORY;
                continue;
            }
            ss->id = smallest_protected + j;
            my_memcpy(ss->source_symbol._whole_data, unknowns[current_unknown], symbol_size);
            source_symbols[j] = ss;
            my_free(cnx, unknowns[current_unknown++]);
        } else if (!source_symbols[j] && (!can_recover || undetermined[current_unknown] || symbol_is_zero(unknowns[current_unknown], symbol_size))) {
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

    return err;
}
