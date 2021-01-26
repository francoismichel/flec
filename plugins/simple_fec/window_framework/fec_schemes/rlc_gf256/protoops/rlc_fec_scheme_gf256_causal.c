#include <picoquic.h>
#include <zlib.h>
#include "../../prng/tinymt32.c"
#include "../gf256/swif_symbol.c"
#include "rlc_fec_scheme_gf256.h"
#include "../../../types.h"



static __attribute__((always_inline)) void swap(uint8_t **a, int i, int j) {
    uint8_t *tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
}

// returns 1 if a has its first non-zero term before b
// returns -1 if a has its first non-zero term after b
// returns 0 otherwise
static __attribute__((always_inline)) int cmp_eq_i(uint8_t *a, uint8_t *b, int idx, int n_unknowns) {
    if (a[idx] < b[idx]) return -1;
    else if (a[idx] > b[idx]) return 1;
    else if (a[idx] != 0) return 0;
    return 0;
}
// returns 1 if a has its first non-zero term before b
// returns -1 if a has its first non-zero term after b
// returns 0 otherwise
static __attribute__((always_inline)) int cmp_eq(uint8_t *a, uint8_t *b, int idx, int n_unknowns) {
    for (int i = 0 ; i < n_unknowns ; i++) {
        int cmp = 0;
        if ((cmp = cmp_eq_i(a, b, i, n_unknowns)) != 0) {
            return cmp;
        }
    }
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

static __attribute__((always_inline)) int first_non_zero_idx(const uint8_t *a, int n_unknowns) {
    for (int i = 0 ; i < n_unknowns ; i++) {
        if (a[i] != 0) {
            return i;
        }
    }
    return -1;
}

/*******
Function that performs Gauss-Elimination and returns the Upper triangular matrix:
There are two options to do this in C.
1. Pass a matrix (a) as the parameter, and calculate and store the upperTriangular(Gauss-Eliminated Matrix) in it.
2. Use malloc and make the function of pointer type and return the pointer.
This program uses the first option.
********/
static __attribute__((always_inline)) void gaussElimination(picoquic_cnx_t *cnx, int n_eq, int n_unknowns, uint8_t **a, uint8_t *constant_terms[n_eq], uint8_t *x[n_eq], bool undetermined[n_unknowns], uint32_t symbol_size, uint8_t **mul, uint8_t *inv){

    PROTOOP_PRINTF(cnx, "START GAUSSIAN\n");
    sort_system(cnx, a, constant_terms, n_eq, n_unknowns);


//    PROTOOP_PRINTF(cnx, "PRINTING SYSTEM BEFORE ELIMINATION\n");
//    for (int g = 0 ; g < n_eq ; g++) {
//        for (int h = 0 ; h < n_unknowns ; h++) {
//            PROTOOP_PRINTF(cnx, "a[%d][%d] = %d\n", g, h, a[g][h]);
//        }
//    }
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
                symbol_sub_scaled(constant_terms[k], term, constant_terms[i], align(symbol_size), mul);
            }
        }
    }
    sort_system(cnx, a, constant_terms, n_eq, n_unknowns);
    PROTOOP_PRINTF(cnx, "PRINTING SYSTEM  BEFORE CANONICAL\n");
    for (int g = 0 ; g < n_eq ; g++) {
        for (int h = 0 ; h < n_unknowns ; h++) {
            PROTOOP_PRINTF(cnx, "a[%d][%d] = %d\n", g, h, a[g][h]);
        }
    }

    for (i = 0 ; i < n_eq-1 ; i++) {
        int first_nz_id = first_non_zero_idx(a[i], n_unknowns);
//        PROTOOP_PRINTF(cnx, "FIRST NZ ID = %ld\n", first_nz_id);
        if (first_nz_id == -1) {
            break;
        }
        for (j = first_nz_id + 1 ; j < n_unknowns && a[i][j] != 0; j++) {
            // let's try to cancel this column from this equation
            for (k = i + 1 ; k < n_eq ; k++) {
                int first_nz_id_below = first_non_zero_idx(a[k], n_unknowns);
                if (j > first_nz_id_below) {
                    break;
                } else if (first_nz_id_below == j) {
                    uint8_t term = gf256_mul(a[i][j], inv[a[k][j]], mul);
                    for (int l = j ; l < n_unknowns ; l++) {
//                        a[i][l] = a[i][l] - a[i][j]*a[k][l]/a[k][j];
                        a[i][l] = gf256_sub(a[i][l], gf256_mul(term, a[k][l], mul));
//                        a[i][l] = a[i][l] - a[i][j]*a[k][l]/a[k][j];
                    }
                    symbol_sub_scaled(constant_terms[i], term, constant_terms[k], align(symbol_size), mul);
                    break;
                }
            }

        }

    }


//    PROTOOP_PRINTF(cnx, "PRINTING SYSTEM\n");
    for (int g = 0 ; g < n_eq ; g++) {
        for (int h = 0 ; h < n_unknowns ; h++) {
            PROTOOP_PRINTF(cnx, "a[%d][%d] = %d\n", g, h, a[g][h]);
        }
    }







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
            if (candidate < 0) {
                // the system cannot be fully recovered, but maybe some parts could be
                PROTOOP_PRINTF(cnx, "THE SYSTEM IS AT LEAST PARTIALLY UNDETERMINED\n");
                break;
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
                        symbol_sub_scaled(x[candidate], a[i][j], x[j], align(symbol_size), mul);
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
//                PROTOOP_PRINTF(cnx, "%u /= %u = %u\n", x[candidate][8], a[i][candidate], mul[x[candidate][8]][inv[a[i][candidate]]]);
                symbol_mul(x[candidate], inv[a[i][candidate]], symbol_size, mul);
                a[i][candidate] = gf256_mul(a[i][candidate], inv[a[i][candidate]], mul);
            }
            candidate--;
        }
    }
    PROTOOP_PRINTF(cnx, "END GAUSSIAN\n");
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
 * \param[in] missing_source_symbols <b> window_source_symbol_id * </b> buffer of missing source symbols IDs
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
    window_source_symbol_id_t *missing_source_symbols = (window_source_symbol_id_t *) get_cnx(cnx, AK_CNX_INPUT, 6);
    window_source_symbol_id_t smallest_protected = (window_source_symbol_id_t) get_cnx(cnx, AK_CNX_INPUT, 7);
    uint16_t symbol_size = (uint16_t) get_cnx(cnx, AK_CNX_INPUT, 8);

//    PROTOOP_PRINTF(cnx, "%d MISSING SOURCE SYMBOLS, TOTAL SYMBOLS = %d\n", n_missing_source_symbols, n_source_symbols);

    if (n_repair_symbols == 0 || n_missing_source_symbols == 0) {
        return 0;
    }


    window_repair_symbol_t *rs;

    // contains the indexes of the unknowns in the system
    int *missing_indexes = my_malloc(cnx, n_source_symbols*sizeof(int));

    my_memset(missing_indexes, -1, n_source_symbols*sizeof(int));

    for (int j = 0 ; j < n_missing_source_symbols ; j++) {
        missing_indexes[missing_source_symbols[j] - smallest_protected] = j;
    }


    // building the system, equation by equation
    bool *protected_symbols = my_malloc(cnx, n_source_symbols*sizeof(bool));
    if (!protected_symbols)
        return PICOQUIC_ERROR_MEMORY;
    my_memset(protected_symbols, 0, n_source_symbols*sizeof(bool));

    int n_trivial = 0;

    bool contains_trivial_equations = false;

    window_source_symbol_id_t *new_missing_source_symbols = NULL;


    if (n_missing_source_symbols > n_repair_symbols) {
        // see if there are symbols that can be trivially recovered
        window_repair_symbol_t **trivial_repairs = my_malloc(cnx, n_repair_symbols*sizeof(repair_symbol_t *));
        if (!trivial_repairs) {
//            PROTOOP_PRINTF(cnx, "CANNOT ALLOCATE\n");
            return PICOQUIC_ERROR_MEMORY;
        }
        PROTOOP_PRINTF(cnx, "TRY TRIVIAL\n");
        my_memset(trivial_repairs, 0, n_repair_symbols*sizeof(repair_symbol_t *));

        new_missing_source_symbols = my_malloc(cnx, n_missing_source_symbols*sizeof(window_source_symbol_id_t));
        int n_new_missing_source_symbols = 0;
        // search for trivial ones
        for_each_window_repair_symbol(repair_symbols, rs, n_repair_symbols) {
                bool trivial = false;
                if (rs && rs->metadata.n_protected_symbols < 10) {

                    window_source_symbol_id_t smallest_protected_by_rs = rs->metadata.first_id;
                    for (int k = smallest_protected_by_rs - smallest_protected;
                         k < smallest_protected_by_rs + rs->metadata.n_protected_symbols - smallest_protected; k++) {

                        if (!source_symbols[k] && !protected_symbols[k]) {
                            int index_in_missing = missing_indexes[k];
                            // check if lower symbols are concerned by this rs
                            trivial =
                                    (index_in_missing == 0 ||
                                     missing_source_symbols[index_in_missing - 1] < smallest_protected_by_rs)
                                    // check if lower symbols are concerned by this rs
                                    && (index_in_missing == n_missing_source_symbols - 1 ||
                                        missing_source_symbols[index_in_missing + 1] >
                                        smallest_protected_by_rs + rs->metadata.n_protected_symbols - 1);
                            if (trivial) {
                                protected_symbols[k] = true;
                                new_missing_source_symbols[n_new_missing_source_symbols++] = k + smallest_protected;
                            }
                        }
                    }
                }
                if (trivial) {
                    trivial_repairs[n_trivial++] = rs;
                }
            }
            if (n_trivial > 0) {
                // this is the new number of symbols to recover
                n_missing_source_symbols = n_new_missing_source_symbols;
                // these are the new symbols to recover
                missing_source_symbols = new_missing_source_symbols;
//                smallest_protected = new_smallest_protected;

                // recompute missing indexes
                for (int j = 0 ; j < n_missing_source_symbols ; j++) {
                    missing_indexes[missing_source_symbols[j] - smallest_protected] = j;
                }
                my_memcpy(repair_symbols, trivial_repairs, n_trivial*sizeof(window_repair_symbol_t *));
                n_repair_symbols = n_trivial;
            }
            my_free(cnx, trivial_repairs);
    }

    PROTOOP_PRINTF(cnx, "N TRIVIAL = %lu\n", n_trivial);

    if (n_trivial == 0 && n_missing_source_symbols > n_repair_symbols) {
        my_free(cnx, missing_indexes);
        my_free(cnx, protected_symbols);
        if (new_missing_source_symbols) {
            my_free(cnx, new_missing_source_symbols);
        }
        return 0;
    }

    my_memset(protected_symbols, 0, n_source_symbols*sizeof(bool));



    uint8_t **mul = fs->table_mul;

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

    if (!coefs || !unknowns || !system_coefs || !undetermined) {
        PROTOOP_PRINTF(cnx, "NOT ENOUGH MEM\n");
        return PICOQUIC_ERROR_MEMORY;
    }

    my_memset(undetermined, 0, n_missing_source_symbols*sizeof(bool));

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

    i = 0;
    tinymt32_t *shuffle_prng = my_malloc(cnx, sizeof(tinymt32_t));
    shuffle_prng->mat1 = 0x8f7011ee;
    shuffle_prng->mat2 = 0xfc78ff1f;
    shuffle_prng->tmat = 0x3793fdff;
    tinymt32_init(shuffle_prng, picoquic_current_time());
    shuffle_repair_symbols(cnx, repair_symbols, n_repair_symbols, shuffle_prng);
    my_free(cnx, shuffle_prng);

//    // indicates the source symbols that can be trivially repaired (a RS only concerns it and not the others lost symbols)
//    repair_symbol_t **trivial_repairs = my_malloc(cnx, n_missing_source_symbols*sizeof(repair_symbol_t *));
//    if (!trivial_repairs) {
//        PROTOOP_PRINTF(cnx, "CANNOT ALLOCATE\n");
//        return PICOQUIC_ERROR_MEMORY;
//    }
//    my_memset(trivial_repairs, 0, n_missing_source_symbols*sizeof(repair_symbol_t *));


    for_each_window_repair_symbol(repair_symbols, rs, n_repair_symbols) {
        if (rs && i < n_eq) {
//            PROTOOP_PRINTF(cnx, "RS %d CRC = 0x%x\n", i, crc32(0, rs->repair_symbol.repair_payload, rs->repair_symbol.payload_length));
            window_source_symbol_id_t smallest_protected_by_rs = rs->metadata.first_id;
            bool protects_at_least_one_new_source_symbol = false;
            bool trivial = false;
            for (int k = smallest_protected_by_rs - smallest_protected ; k < smallest_protected_by_rs + rs->metadata.n_protected_symbols - smallest_protected ; k++) {
                if (!source_symbols[k]) {
                    int index_in_missing = missing_indexes[k];
                                 // check if lower symbols are concerned by this rs
                    trivial =  (index_in_missing == 0 || missing_source_symbols[index_in_missing-1] < smallest_protected)
                                // check if lower symbols are concerned by this rs
                                && (index_in_missing == n_missing_source_symbols - 1 || missing_source_symbols[index_in_missing+1] > smallest_protected_by_rs + rs->metadata.n_protected_symbols - 1);
//                    PROTOOP_PRINTF(cnx, "RS [%u] IS TRIVIAL ?\n", smallest_protected_by_rs);
                    if (trivial) {
//                        PROTOOP_PRINTF(cnx, "TRIVIAL\n");
                        contains_trivial_equations = true;
                        break;
                    }
                }
                // this source symbol is protected by this repair symbol
                if (!source_symbols[k] && !protected_symbols[k]) {
                    protects_at_least_one_new_source_symbol = true;
                    protected_symbols[k] = true;
                    break;
                }
            }
            if (trivial || protects_at_least_one_new_source_symbol) {
                PROTOOP_PRINTF(cnx, "RS %u PROTECTS ONE SS\n", decode_u32(rs->metadata.fss.val));
                constant_terms[i] = my_malloc(cnx, symbol_size);
                if (!constant_terms[i]) {
                    return -1;
                }
//                PROTOOP_PRINTF(cnx, "MALLOC DONE, RS = %p\n", (protoop_arg_t) rs);
                my_memset(constant_terms[i], 0, symbol_size);
                my_memcpy(constant_terms[i], rs->repair_symbol.repair_payload, symbol_size);
                my_memset(system_coefs[i], 0, n_missing_source_symbols);
                get_coefs(cnx, prng, decode_u32(rs->metadata.fss.val), rs->metadata.n_protected_symbols, &coefs[smallest_protected_by_rs - smallest_protected]);
                int current_unknown = 0;
//                PROTOOP_PRINTF(cnx, "BEFORE LOOP, source_symbols = %p\n", (protoop_arg_t) source_symbols);
                for (int j = smallest_protected_by_rs - smallest_protected ; j < smallest_protected_by_rs + rs->metadata.n_protected_symbols - smallest_protected ; j++) {
                    // this source symbol is protected by this repair symbol
                    if (source_symbols[j]) {
//                        PROTOOP_PRINTF(cnx, "SYMBOL %u NOT NULL\n", j + smallest_protected);
//                        PROTOOP_PRINTF(cnx, "ADD KNOWN TO CT, COEF = %u, CRC = 0x%x\n", coefs[j], crc32(0, source_symbols[j]->source_symbol._whole_data, symbol_size));
                        symbol_sub_scaled(constant_terms[i], coefs[j], source_symbols[j]->source_symbol._whole_data, align(symbol_size), mul);
                    } else if (current_unknown < n_missing_source_symbols) {
//                        PROTOOP_PRINTF(cnx, "ADDING UNKNOWN %u, COEF %u\n", j + smallest_protected, coefs[j]);
//                        system_coefs[i][current_unknown++] = coefs[j];
                        if (missing_indexes[j] != -1) {
                            system_coefs[i][missing_indexes[j]] = coefs[j];
                            current_unknown++;
                        } else {
                            PROTOOP_PRINTF(cnx, "ERROR: WRONG INDEX FOR ID %u\n", i + smallest_protected);
                        }
                    }
                }
                i++;
                PROTOOP_PRINTF(cnx, "EQUATION BUILT\n");
            }
        }
    }
    my_free(cnx, protected_symbols);
    int n_effective_equations = i;
    PROTOOP_PRINTF(cnx, "SYSTEM BUILT, %lu EFFECTIVE EQUATIONS, %lu MISSING SS, CONTAINS TRIVIAL = %d\n", n_effective_equations, n_missing_source_symbols, contains_trivial_equations);

    // the system is built: let's recover it
    bool can_recover = contains_trivial_equations || n_effective_equations >= n_missing_source_symbols;
    if (can_recover)
        gaussElimination(cnx, n_effective_equations, n_missing_source_symbols, system_coefs, constant_terms, unknowns, undetermined, symbol_size, mul, fs->table_inv);
    else
        PROTOOP_PRINTF(cnx, "CANNOT RECOVER, %d EQUATIONS, %d MISSING SYMBOLS %lu RS\n", n_effective_equations, n_missing_source_symbols, n_repair_symbols);
    PROTOOP_PRINTF(cnx, "END GAUSSIAN\n");
    int current_unknown = 0;
    int err = 0;
    for (int j = 0 ; j < n_missing_source_symbols ; j++) {
        int idx = missing_source_symbols[j] - smallest_protected;
//        PROTOOP_PRINTF(cnx, "missing[j] = %u\n", (protoop_arg_t) missing_source_symbols[j]);
        if (!source_symbols[idx] && can_recover && !undetermined[current_unknown] && !symbol_is_zero(unknowns[current_unknown], symbol_size)) {
            // TODO: handle the case where source symbols could be 0
            window_source_symbol_t *ss = create_window_source_symbol(cnx, symbol_size);
            if (!ss) {
                my_free(cnx, unknowns[current_unknown++]);
                err = PICOQUIC_ERROR_MEMORY;
                continue;
            }
            ss->id = smallest_protected + idx;
            my_memcpy(ss->source_symbol._whole_data, unknowns[current_unknown], symbol_size);
            source_symbols[idx] = ss;
            my_free(cnx, unknowns[current_unknown++]);
            PROTOOP_PRINTF(cnx, "RECOVERED SYMBOL %u, CRC = 0x%x\n", ss->id, crc32(0, ss->source_symbol._whole_data, symbol_size));
        } else if (!source_symbols[idx] && (!can_recover || undetermined[current_unknown] || symbol_is_zero(unknowns[current_unknown], symbol_size))) {
            // this unknown could not be recovered
            my_free(cnx, unknowns[current_unknown++]);
        }
    }

    set_cnx(cnx, AK_CNX_OUTPUT, 0, can_recover);

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
    my_free(cnx, missing_indexes);
    if (new_missing_source_symbols)
        my_free(cnx, new_missing_source_symbols);

//    PROTOOP_PRINTF(cnx, "END RECOVER: ELAPSED %luµs\n", picoquic_current_time() - now);
    return err;
}
