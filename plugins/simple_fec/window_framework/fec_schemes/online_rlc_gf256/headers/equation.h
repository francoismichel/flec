//
// Created by michelfra on 10/04/20.
//

#ifndef ONLINE_GAUSSIAN_EQUATION_H
#define ONLINE_GAUSSIAN_EQUATION_H

#include <stdint.h>
#include <stdbool.h>

#define ALIGNMENT 32


#include "../headers/symbol.h"
#include "util.h"
#include "../gf256/swif_symbol.h"
#include "../../../types.h"

#define MAX_UNKNOWNS 500

typedef struct {
    source_symbol_id_t pivot;   // id of the pivot (i.e. first non-zero coefficient in the equation)
    source_symbol_id_t last_non_zero_id;
    window_repair_symbol_t constant_term;
    uint32_t n_coefs;
    coef_t *coefs;
    size_t _coefs_allocated_size;
} equation_t;



static __attribute__((always_inline)) bool equation_includes_id(equation_t* eq,
                                        source_symbol_id_t symbol_id)
{
    return (symbol_id >= eq->constant_term.metadata.first_id && symbol_id <= repair_symbol_last_id(&eq->constant_term));
}


/**
 * @brief get the minimum source index that appears in the symbol
 *        SYMBOL_ID_NONE if there is none (e.g. symbol is 0)
 */
static __attribute__((always_inline)) uint32_t equation_get_min_symbol_id(equation_t *eq)
{
    return eq->pivot;
}


/**
 * @brief get the maximum source index that appears in the symbol
 *        returns SYMBOL_ID_NONE if there is none (e.g. symbol is 0)
 */
static __attribute__((always_inline)) uint32_t equation_get_max_symbol_id(equation_t *eq)
{
    return eq->last_non_zero_id;
}

static __attribute__((always_inline)) uint32_t equation_count_allocated_coef(equation_t *equation)
{
    if (equation->pivot == SYMBOL_ID_NONE) {
        assert(equation->last_non_zero_id == SYMBOL_ID_NONE);
        return 0;
    } else {
        return equation->constant_term.metadata.n_protected_symbols;
    }
}

static __attribute__((always_inline)) coef_t equation_get_coef(equation_t *eq, source_symbol_id_t i) {
    // if out of bounds, the coef is 0
    if (i < eq->constant_term.metadata.first_id || repair_symbol_last_id(&eq->constant_term) < i) {
        return 0;
    }
    uint32_t idx = i - eq->constant_term.metadata.first_id;
    return eq->coefs[idx];
}

/**
 * update full symbol max coef
 */
static __attribute__((always_inline)) bool full_symbol_adjust_max_coef(equation_t* eq)
{
    assert(eq->constant_term.first_id != SYMBOL_ID_NONE
           && eq->constant_term.n_protected_symbols > 0 );

    bool result = false;
    eq->last_non_zero_id = SYMBOL_ID_NONE;

    for (source_symbol_id_t j = 0; j < equation_count_allocated_coef(eq); j++) {
        source_symbol_id_t i = repair_symbol_last_id(&eq->constant_term) - j;
        if (equation_get_coef(eq, i) != 0){
            eq->last_non_zero_id = i;
            result = true;
            break;
        }
    }
    return result;
}
/**
 * update full symbol min coef
 */
static __attribute__((always_inline)) bool full_symbol_adjust_min_coef(equation_t *eq)
{
    assert( eq->constant_term.first_id != SYMBOL_ID_NONE
            && eq->constant_term.n_protected_symbols > 0 );

    bool result = false;
    eq->pivot = SYMBOL_ID_NONE;

    bool adjust_fast = true;
    if (adjust_fast) {
        uint8_t *coefs = eq->coefs;
        uint64_t *coefs_64 = (uint64_t *) eq->coefs;
        int64_t i;
        for (i = 0 ; result != true && i < eq->constant_term.metadata.n_protected_symbols/sizeof(uint64_t) ; i++) {
            if(coefs_64[i] != 0) {
                for (int64_t j = i*sizeof(uint64_t) ; j < (i+1)*sizeof(uint64_t) ; j++) {
                    if(coefs[j] != 0) {
                        eq->pivot = j + eq->constant_term.metadata.first_id;
                        result = true;
                        break;
                    }
                }
            }
        }
        if (result != true) {
            for (int64_t j = i*sizeof(uint64_t) ; j < (i+1)*sizeof(uint64_t) && j < eq->constant_term.metadata.n_protected_symbols ; j++) {
                if(coefs[j] != 0) {
                    eq->pivot = j + eq->constant_term.metadata.first_id;
                    result = true;
                    break;
                }
            }
        }

    } else {
        for (source_symbol_id_t i = eq->constant_term.metadata.first_id; i <= repair_symbol_last_id(&eq->constant_term); i++) {
            if (equation_get_coef(eq, i) != 0){
                eq->pivot = i;
                result = true;
                break;
            }
        }
    }
    return result;
}




/**
 *update full symbol min and max coefs
 *  returns whether there exists non-zero coefs
 */

static __attribute__((always_inline)) bool equation_adjust_non_zero_bounds(equation_t *eq)
{
    if (eq->constant_term.metadata.first_id == SYMBOL_ID_NONE) {
        assert( eq->constant_term.n_protected_symbols == 0 );
        eq->pivot = SYMBOL_ID_NONE;
        eq->last_non_zero_id = SYMBOL_ID_NONE;
        return false;
    }
    bool result1 = full_symbol_adjust_min_coef(eq);
    bool result2 = full_symbol_adjust_max_coef(eq);
    assert(result1 == result2);
    return result1;
}

/**
 * @brief Returns whether the symbol is an empty symbol
 */
static __attribute__((always_inline)) bool equation_is_zero(equation_t *eq)
{
    return equation_get_min_symbol_id(eq) == SYMBOL_ID_NONE;
}


/**
 * @brief Returns whether the symbol is an empty symbol
 */
static __attribute__((always_inline)) bool equation_has_one_id(equation_t *full_symbol)
{
    return (!equation_is_zero(full_symbol))
           && (equation_get_min_symbol_id(full_symbol)
               == equation_get_max_symbol_id(full_symbol));
}
/**
 * @brief Release a full_symbol
 */
static __attribute__((always_inline)) void equation_free_keep_repair_payload(picoquic_cnx_t *cnx, equation_t* eq)
{
    assert(eq->coefs != NULL);
    free_fn(cnx, eq->coefs);
    eq->coefs = NULL;
    assert(eq->constant_term.data != NULL);
    eq->constant_term.repair_symbol.repair_payload = NULL;
    free_fn(cnx, eq);
}
/**
 * @brief Release a full_symbol
 */
static __attribute__((always_inline)) void equation_free(picoquic_cnx_t *cnx, equation_t* eq)
{
    uint8_t *repair_payload = eq->constant_term.repair_symbol.repair_payload;
    equation_free_keep_repair_payload(cnx, eq);
    free_fn(cnx, repair_payload);
}

static __attribute__((always_inline)) uint32_t equation_get_coef_index(equation_t *eq, source_symbol_id_t id) {
    assert(equation_includes_id(eq, id));
    return id-eq->constant_term.metadata.first_id;
}

static __attribute__((always_inline)) equation_t *equation_alloc_with_given_data
        (picoquic_cnx_t *cnx, window_repair_symbol_t *rs) {
    window_source_symbol_id_t first_id_id = rs->metadata.first_id;
    size_t n_coefs;
    if (first_id_id == SYMBOL_ID_NONE) {
        assert(last_symbol_id == SYMBOL_ID_NONE);
        n_coefs = 1; /* Actually 0, but we never want a NULL pointer,
                               and malloc(0) might be NULL or not */
    } else {
        assert(first_id_id <= last_symbol_id);
        n_coefs = rs->metadata.n_protected_symbols;
    }

    /* allocate result */
    equation_t *result
            = (equation_t *)calloc_fn(cnx, 1, sizeof(equation_t));
    if (result == NULL) {
        free_fn(cnx, result);
        return NULL;
    }
    /* allocate coef and data, round the size to a multiple of 2*ALIGNEMENT to ensure no everlap */
    uint8_t *coefs = (uint8_t *)calloc_fn(cnx, align(n_coefs), sizeof(uint8_t));
    if (coefs == NULL) {
        /* free the structure in case of problem */
        free_fn(cnx, result);
        return NULL;
    }
    result->_coefs_allocated_size = align(n_coefs);
    result->coefs = coefs;
    result->n_coefs = n_coefs;


    /* fill content */
    result->constant_term = *rs;
    equation_adjust_non_zero_bounds(result);

    return result;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Create a full_symbol from a raw packet (a set of bytes)
 *        and initialize it with content '0'
 */
static __attribute__((always_inline)) equation_t *equation_alloc
        (picoquic_cnx_t *cnx, source_symbol_id_t first_id_id, source_symbol_id_t last_symbol_id, uint32_t symbol_size)
{

    uint32_t safe_symbol_size = symbol_size;
    if (safe_symbol_size == 0) {
        safe_symbol_size = 1; /* because calloc_fn(0,...) can return NULL */
    }

    uint8_t *data = (uint8_t *)calloc_fn(cnx,  align(safe_symbol_size), sizeof(uint8_t));
    if (data == NULL) {
        /* free the structure in case of problem */
        PROTOOP_PRINTF(cnx, "could not alloc data\n");
        return NULL;
    }

    window_repair_symbol_t rs;
    rs.metadata.first_id = first_id_id;
    rs.metadata.n_protected_symbols = last_symbol_id + 1 - first_id_id;
    rs.repair_symbol.repair_payload = data;

    equation_t *result = equation_alloc_with_given_data(cnx, &rs);

    return result;
}

/**
 * @brief Create a full_symbol from a raw packet (a set of bytes)
 *        and initialize it with content '0'
 */
static __attribute__((always_inline)) equation_t *equation_create_from_source
        (picoquic_cnx_t *cnx, uint32_t symbol_id, uint8_t *symbol_data, uint32_t symbol_size)
{
    equation_t *equation = equation_alloc(cnx, symbol_id, symbol_id, symbol_size);
    source_symbol_id_t coef_index = equation_get_coef_index(equation, symbol_id);
    equation->coefs[coef_index] = 1;
    equation_adjust_non_zero_bounds(equation);
    memcpy_fn(equation->constant_term.repair_symbol.repair_payload, symbol_data, symbol_size);
    equation->constant_term.repair_symbol.payload_length = symbol_size;
    return equation;
}
/**
 * @brief Create a full_symbol from a raw packet (a set of bytes)
 *        and initialize it with content '0'
 */
static __attribute__((always_inline)) equation_t *equation_create_from_source_zerocopy
        (picoquic_cnx_t *cnx, uint32_t symbol_id, uint8_t *symbol_data, uint32_t symbol_size)
{
    window_repair_symbol_t rs;
    memset_fn(&rs, 0, sizeof(window_repair_symbol_t));
    rs.metadata.first_id = symbol_id;
    rs.metadata.n_protected_symbols = 1;
    rs.repair_symbol.repair_payload = symbol_data;
    rs.repair_symbol.payload_length = symbol_size;

    equation_t *equation = equation_alloc_with_given_data(cnx, &rs);
    source_symbol_id_t coef_index = equation_get_coef_index(equation, symbol_id);
    equation->coefs[coef_index] = 1;
    equation_adjust_non_zero_bounds(equation);
    equation->constant_term.repair_symbol.payload_length = symbol_size;
    return equation;
}

static __attribute__((always_inline)) void equation_clear_unused_coefs(equation_t *eq) {
    if (equation_is_zero(eq)) {
        return;
    }
    window_source_symbol_id_t start_index = eq->last_non_zero_id + 1 - eq->constant_term.metadata.first_id;
    my_memset(&eq->coefs[start_index], 0, eq->_coefs_allocated_size - start_index);
}

// pre: eq1 can welcome eq2 in its coefs buffer
static __attribute__((always_inline)) void add_coefs(picoquic_cnx_t *cnx, equation_t *eq1, equation_t *eq2, source_symbol_id_t from, source_symbol_id_t to) {
    from = MAX(from, eq2->constant_term.metadata.first_id);
    to = MIN(to, repair_symbol_last_id(&eq2->constant_term));
    uint8_t *eq1_coefs_buffer = &eq1->coefs[from - eq1->constant_term.metadata.first_id];
    uint8_t *eq2_coefs_buffer = &eq2->coefs[from - eq2->constant_term.metadata.first_id];

    symbol_add(eq1_coefs_buffer, eq2_coefs_buffer, MIN(to + 1 - from, eq1->constant_term.metadata.n_protected_symbols));
//    eq1->coefs[i-first_rs_id] = equation_get_coef(eq1, i) ^ equation_get_coef(eq2, i);

}




static __attribute__((always_inline)) void equation_multiply(equation_t  *eq, coef_t coef, uint8_t **mul_table) {
    // multiply the coefficients of the equation (we can do it with one call)
    symbol_mul(eq->coefs, coef, align(eq->constant_term.metadata.n_protected_symbols), mul_table);
    // multiply the constant term of the equation
    symbol_mul(eq->constant_term.repair_symbol.repair_payload, coef, align(eq->constant_term.repair_symbol.payload_length), mul_table);
}




/**
 * @brief Take a symbol and add another symbol to it, e.g. performs the equivalent of: p3 = p1 + p2
 * @param[in] p1     First equation (to which p2 will be added): must be able to contain eq2's coefs and data
 * @param[in] p2     Second equation
 *
 * eq1, eq2 should not be zero
 */
static __attribute__((always_inline)) void full_symbol_add_base(picoquic_cnx_t *cnx, equation_t *eq1, equation_t *eq2)
{
    assert (eq1->constant_term.data != NULL && eq2->constant_term.data != NULL);
    assert (eq1->constant_term.data_length >= eq2->constant_term.data_length);
    assert (equation_includes_id(eq1, eq1->pivot));
    assert (equation_includes_id(eq1, eq1->last_non_zero_id));
    assert (equation_includes_id(eq1, eq2->pivot));
    assert (equation_includes_id(eq1, eq2->last_non_zero_id));

//    uint32_t first_coef_index;
//    uint32_t last_coef_index;

    // XXX: should not be NONE
    if (eq1->pivot == SYMBOL_ID_NONE && eq2->pivot == SYMBOL_ID_NONE ){
        eq1->pivot = SYMBOL_ID_NONE;
        eq1->last_non_zero_id = SYMBOL_ID_NONE;
    }
    if (eq1->last_non_zero_id > eq2->last_non_zero_id){
        equation_clear_unused_coefs(eq2);
    }
    bool add_fast = true;
    if (!add_fast) {

        source_symbol_id_t first_rs_id = eq1->constant_term.metadata.first_id;
        for (uint32_t i = eq2->pivot; i <= eq2->last_non_zero_id; i++){
            eq1->coefs[i-first_rs_id] = equation_get_coef(eq1, i) ^ equation_get_coef(eq2, i);
        }
    } else {

        add_coefs(cnx, eq1, eq2, eq2->pivot, eq2->last_non_zero_id);
    }

    equation_adjust_non_zero_bounds(eq1);
    symbol_add((void *)eq1->constant_term.repair_symbol.repair_payload, (void *)eq2->constant_term.repair_symbol.repair_payload, align(eq2->constant_term.repair_symbol.payload_length));
}


/**
 * @brief Take a symbol and add another symbol to it, e.g. performs the equivalent of: p1 + p2
 * @param[in] p1     First symbol (to which p2 will be added) !!! CANNOT BE ZERO
 * @param[in] p2     Second symbol
 * The result is placed in eq1
 *
 * returns 0 if no error happened
 */
static __attribute__((always_inline)) int equation_add(picoquic_cnx_t *cnx, equation_t *eq1, equation_t* eq2)
{
    uint32_t first_coef_index;
    uint32_t last_coef_index;
    if (equation_is_zero(eq1) && equation_is_zero(eq2)) {
        /* return 0 */
        return 0;
    }
    if (equation_is_zero(eq2)) {
        // nothing changes
        return 0;
    }
    if (eq1->pivot <= eq2->pivot) {
        first_coef_index = eq1->pivot ;
    }
    else {
        first_coef_index = eq2->pivot ;
    }

    if (eq1->last_non_zero_id >= eq2->last_non_zero_id){
        last_coef_index = eq1->last_non_zero_id;
    }
    else {
        last_coef_index = eq2->last_non_zero_id;
    }

    // realloc the coefficients if needed
    // quite likely to enter in this if
    if (eq1->n_coefs < (last_coef_index + 1) - first_coef_index) {

        uint32_t new_size = align(MAX(eq1->_coefs_allocated_size, (last_coef_index + 1) - first_coef_index));
        uint8_t *new_coefs = malloc_fn(cnx, new_size);
        if (!new_coefs) {
            return -1;
        }
        memset_fn(new_coefs, 0, new_size);
        // copy the old coefs in the new array at the right place (same place as before)
        memcpy_fn(&new_coefs[equation_get_coef_index(eq1, eq1->pivot)], &eq1->coefs[equation_get_coef_index(eq1, eq1->pivot)], (eq1->last_non_zero_id + 1) - eq1->pivot);
        free_fn(cnx, eq1->coefs);
        eq1->coefs = new_coefs;
        eq1->_coefs_allocated_size = new_size;
        eq1->n_coefs = (last_coef_index + 1) - first_coef_index;
        // no need to update the pivot bounds right now

    }
    // here, the repair symbol has changed, it only concerns the ids with a non-zero coefficient in eq1 or eq2, so we update it
    if (eq1->constant_term.metadata.first_id <= first_coef_index && repair_symbol_last_id(&eq1->constant_term) < last_coef_index) {
        uint32_t new_n_nonzero_symbols = repair_symbol_last_id(&eq1->constant_term) + 1 - first_coef_index;

        memmove_fn(eq1->coefs, &eq1->coefs[first_coef_index - eq1->constant_term.metadata.first_id], new_n_nonzero_symbols);
        if (new_n_nonzero_symbols < eq1->n_coefs) {
            memset_fn(&eq1->coefs[new_n_nonzero_symbols], 0, eq1->n_coefs - new_n_nonzero_symbols);
        }
        eq1->constant_term.metadata.first_id = first_coef_index;
        eq1->constant_term.metadata.n_protected_symbols = (last_coef_index + 1) - first_coef_index;
        equation_adjust_non_zero_bounds(eq1);
    }


    uint32_t data_length = (eq1->constant_term.repair_symbol.payload_length >= eq2->constant_term.repair_symbol.payload_length)
                           ? eq1->constant_term.repair_symbol.payload_length : eq2->constant_term.repair_symbol.payload_length;

    // realloc the constant term if needed
    // unlikely to enter in this if
    if (data_length > eq1->constant_term.repair_symbol.payload_length) {
        uint8_t *new_data = realloc_fn(cnx, eq1->constant_term.repair_symbol.repair_payload, data_length);
        if (!new_data) {
            return -1;
        }
        memset_fn(&new_data[eq1->constant_term.repair_symbol.payload_length], 0, data_length - eq1->constant_term.repair_symbol.payload_length);
        eq1->constant_term.repair_symbol.repair_payload = new_data;
        eq1->constant_term.repair_symbol.payload_length = data_length;
    }

    // results stored in eq1
    full_symbol_add_base(cnx, eq1, eq2);

    return 0;
}

#endif //ONLINE_GAUSSIAN_EQUATION_H
