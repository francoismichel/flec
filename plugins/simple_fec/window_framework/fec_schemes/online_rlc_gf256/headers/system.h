
#ifndef ONLINE_GAUSSIAN_SYSTEM_H
#define ONLINE_GAUSSIAN_SYSTEM_H

#include "equation.h"

typedef struct {
    int max_equations;
    int n_equations;
    source_symbol_id_t first_id_id;
    source_symbol_id_t last_symbol_id;
    equation_t **equations;
} system_t;

#define ENTRY_INDEX_NONE 0xfffffffful
#define SYSTEM_INITIAL_SIZE 2000;

static __attribute__((always_inline)) int system_resize(picoquic_cnx_t *cnx, system_t *system, int new_max_equations) {
    if (new_max_equations > system->max_equations) {
        system->equations = (equation_t **) realloc_fn(cnx, system, new_max_equations*sizeof(equation_t *));
        if (system->equations == NULL) {
            return -1;
        }
    }
    system->max_equations = new_max_equations;
    return 0;
}

static __attribute__((always_inline)) equation_t *system_get_pivot_for_id(picoquic_cnx_t *cnx, system_t *system, source_symbol_id_t id) {
    if (id >= system->first_id_id
        && id < (system->max_equations + system->first_id_id)
        && system->equations[id - system->first_id_id] ){
        return system->equations[id - system->first_id_id];
    }
//    PROTOOP_PRINTF(cnx, "Pivot of symbol id %u is not found \n", id);
    return NULL;
}

static __attribute__((always_inline)) equation_t *system_remove_pivot_for_id(picoquic_cnx_t *cnx, system_t *system, source_symbol_id_t id) {
    if (id >= system->first_id_id
        && id < (system->max_equations + system->first_id_id)
        && system->equations[id - system->first_id_id] ){
        equation_t *ret = system->equations[id - system->first_id_id];
        system->equations[id - system->first_id_id]= NULL;
        return ret;
    }
//    PROTOOP_PRINTF(cnx, "Pivot of symbol id %u is not found \n", id);
    return NULL;
}

static __attribute__((always_inline)) equation_t *system_get_normalized_pivot_for_id(picoquic_cnx_t *cnx, system_t *system, source_symbol_id_t id, uint8_t *inv_table, uint8_t **mul_table) {
    equation_t *eq = system_get_pivot_for_id(cnx, system, id);
    if (!eq) {
        return NULL;
    }
    uint8_t pivot_coef = equation_get_coef(eq, eq->pivot);
    if (pivot_coef != 1) {
        equation_multiply(eq, inv_table[pivot_coef], mul_table);
    }
    return eq;
}

static __attribute__((always_inline)) system_t *system_alloc(picoquic_cnx_t *cnx)
{
    /* allocate the struct */
    system_t *result
            = (system_t *)calloc_fn(cnx, 1, sizeof(system_t));

    if (result == NULL) {
        return NULL;
    }

    /* allocate the table of pointers to full_symbol */
    result->max_equations = SYSTEM_INITIAL_SIZE;
    result->n_equations = 0;

    equation_t **equations
            = calloc_fn(cnx, result->max_equations, sizeof(equation_t *));
    if (equations == NULL) {
        free_fn(cnx, equations);
        return NULL;
    }
    result->equations = equations;
    result->first_id_id = SYMBOL_ID_NONE ;
    result->last_symbol_id = SYMBOL_ID_NONE;
    return result;
}

static __attribute__((always_inline)) int reduce_equation(picoquic_cnx_t *cnx, system_t *system, equation_t *eq, uint8_t **mul_table, uint8_t *inv_table) {

    equation_adjust_non_zero_bounds(eq);
    if (eq->pivot == SYMBOL_ID_NONE)
        return 0;

    int err = 0;
    for (uint32_t id = eq->pivot ; id <= eq->last_non_zero_id && !equation_is_zero(eq); id++) {
        uint8_t coef = equation_get_coef(eq, id);
        if (coef != 0) {
            equation_t *pivot_equation = system_get_pivot_for_id(cnx, system, id);
            if (pivot_equation != NULL) {
//                equation_multiply(pivot_equation, coef, mul_table);
                /* we cancel the coef */

                equation_multiply(eq, mul_table[equation_get_coef(pivot_equation, pivot_equation->pivot)][inv_table[coef]], mul_table);

                // we reduce the equation and remove its pivot coefficient by adding the multiplied equation and the system's pivot equation
                err = equation_add(cnx, eq, pivot_equation);
                if (err) {
                    break;
                }
            }
        }
    }

    return err;
}

static __attribute__((always_inline)) bool system_set_bounds
        (picoquic_cnx_t *cnx, system_t *system, source_symbol_id_t first, source_symbol_id_t last) {
    if (last + 1 - first > system->max_equations) {
        PROTOOP_PRINTF(cnx, "ERROR: BOUNDS TOO LARGE FOR SYSTEM (%u > %u)\n", last + 1 - first, system->max_equations);
        return false;
    }
    if (system->first_id_id < first) {
        PROTOOP_PRINTF(cnx, "ERROR: FIRST BOUND TOO HIGH COMPARED TO THE EXISTING SYSTEM\n");
        return false;
    }
    system->first_id_id = first;
    if (last < system->last_symbol_id && system->last_symbol_id != SYMBOL_ID_NONE) {
        PROTOOP_PRINTF(cnx, "ERROR: LAST BOUND TOO LOW COMPARED TO THE EXISTING SYSTEM\n");
        return false;
    }
    system->last_symbol_id = last;
    return true;
}


/**
 * @brief Add a full_symbol to a packet set.
 *
 * Gaussian elimination can occur.
 * Return the pivot [remove_each_pivot associated to the new full_symbol]
 * or SYMBOL_ID_NONE if dependent (e.g. redundant) packet
 *
 * The full_symbol is not freed and also reference is not captured.
 */

static __attribute__((always_inline)) uint32_t system_add
        (picoquic_cnx_t *cnx, system_t *system, equation_t *eq, equation_t **removed)
{
    assert(system != NULL);
    *removed = NULL;
    if (equation_is_zero(eq)) {
        return ENTRY_INDEX_NONE;
    }

    if (system->first_id_id == SYMBOL_ID_NONE) {
        system->first_id_id = eq->pivot;
    }

    uint32_t old_size = system->max_equations;
    source_symbol_id_t new_i0 = eq->pivot;
    source_symbol_id_t set_i0 = system->first_id_id;


    /* The added symbol has first nonzero index outside the current set */
    if (new_i0 < set_i0) {
        PROTOOP_PRINTF(cnx, "Debugging is enabled. Case: full_symbol_cloned->first_nonzero_id < set->first_id_id \n");
        if (true || set_i0 - new_i0 < system->max_equations) {
            int n_removed_equations = 0;
            for (int i = system->max_equations - (set_i0 - new_i0) ; i < system->last_symbol_id + 1 - system->first_id_id ; i++) {
                if (system->equations[i] != NULL) {
                    n_removed_equations++;
                }
            }
            memmove_fn(system->equations + (set_i0 - new_i0), system->equations,
                       sizeof(equation_t *) * (system->max_equations - (set_i0 - new_i0)));
            memset_fn(system->equations, 0,
                      sizeof(equation_t *)*(set_i0-new_i0));
            system->n_equations -= n_removed_equations;
        } else {
            if (set_i0 - new_i0 < (system->max_equations * 2)) {
                system->max_equations *= 2;
            } else {
                system->max_equations = set_i0 - new_i0 + 1;
            }
            system->equations = realloc_fn(cnx, system->equations, system->max_equations * sizeof(equation_t *));
            memmove(system->equations + (set_i0 - new_i0), system->equations, sizeof(equation_t *) * old_size);
            memset(system->equations, 0,
                   sizeof(equation_t *)*(set_i0-new_i0));
        }
//        IF_DEBUG(full_symbol_set_dump(system, stdout));
        if (system->equations == NULL) {
//            WARNING_PRINT("failed to reallocate equations");
            equation_free(cnx, eq);
            free_fn(cnx, system->equations);
            return ENTRY_INDEX_NONE;
        }
        system->equations[0] = eq;
        system->n_equations++;
        system->first_id_id = MIN(set_i0, new_i0); /* actually should be new_i0 */
        if (system->last_symbol_id == SYMBOL_ID_NONE) {
            system->last_symbol_id = eq->last_non_zero_id;
        } else {
            system->last_symbol_id = MAX(eq->last_non_zero_id, system->last_symbol_id);
        }
        set_i0 = system->first_id_id;
        return new_i0 - set_i0; /* should be 0 */
    }

    PROTOOP_PRINTF(cnx, "Debugging is enabled. Case: full_symbol_cloned->first_nonzero_id > set->first_id_id \n");
    uint32_t idx_pos = new_i0 - set_i0;
    if (idx_pos < system->max_equations) {
        if (system->equations[idx_pos] != NULL) {
            PROTOOP_PRINTF(cnx, "overwriting one full_symbol in set\n");
            *removed = system->equations[idx_pos];
            system->n_equations--;
//            equation_free(cnx, system->equations[idx_pos]);
        }
        system->equations[idx_pos] = eq;
        system->n_equations++;
        if (system->last_symbol_id == SYMBOL_ID_NONE) {
            system->last_symbol_id = eq->last_non_zero_id;
        } else {
            system->last_symbol_id = MAX(eq->last_non_zero_id, system->last_symbol_id);
        }
        return idx_pos;
    }
    PROTOOP_PRINTF(cnx, "ERROR: COULD NOT ADD EQUATION IN FULL SYSTEM, IDX = %u, MAX EQ = %u\n", idx_pos, system->max_equations);
    assert(idx_pos < system->max_equations);

    return ENTRY_INDEX_NONE;
}

static __attribute__((always_inline)) uint32_t system_add_as_pivot
        (picoquic_cnx_t *cnx, system_t *system, equation_t *eq, uint8_t *inv_table, uint8_t **mul_table, int *decoded, equation_t **removed)
{
    *decoded = 0;
    equation_adjust_non_zero_bounds(eq);
    if (eq->pivot == SYMBOL_ID_NONE) {
        return 0;
    }
    source_symbol_id_t first_id = eq->pivot;

    int n_non_null_equations = 0;
    for(uint32_t i = 0 ; i < system->max_equations && n_non_null_equations < system->n_equations; i++) {
        if (system->equations[i]) {
            n_non_null_equations++;
        }
        // TODO: add one temp equation to store the add and mul results  (it could belong to the system) so that we avoid doing inv each time
        // TODO: instead we could also, instead of doing inv to reset at 1, multiplying  by coef*inv[pivot] instead of just coef and remove the multiplication by inv !
        if (system->equations[i] && equation_get_coef(system->equations[i], first_id) != 0) {
            uint8_t coef = equation_get_coef(system->equations[i], first_id); /* XXX: if coef == 0, nothing to do */
            if (coef != 0) {

//            equation_multiply(eq, inv_table[equation_get_coef(eq, eq->pivot)], mul_table);
                uint8_t pivot_coef = equation_get_coef(eq, eq->pivot);
                assert(mul_table[inv_table[pivot_coef]][coef] != 0);
                equation_multiply(eq, mul_table[inv_table[pivot_coef]][coef], mul_table);

                bool has_one_id_before_add = equation_has_one_id(system->equations[i]);
                int err = equation_add(cnx, system->equations[i], eq);
                bool is_decoded = !has_one_id_before_add && equation_has_one_id(system->equations[i]);
                if (is_decoded) {
                    source_symbol_id_t si = equation_get_min_symbol_id(system->equations[i]);
                    if (equation_get_coef(system->equations[i], si)  != 1) {
                        equation_multiply(system->equations[i], inv_table[equation_get_coef(system->equations[i], si)], mul_table);
                    }
                    assert(equation_get_coef(system->equations[i], si)  == 1);
                    *decoded = 1;
                }
            }
        }
    }
// TODO: at this point, eq's pivot could be different from 1. I don't think this is a problem, the application must just ask for a normalized source symbol
    return system_add(cnx, system, eq, removed);
}


// TODO: remember the decoded equations to then get the symbol
//
static __attribute__((always_inline)) int re_adjust_system_to(picoquic_cnx_t *cnx, system_t *system, source_symbol_id_t id) {
    source_symbol_id_t n_equations_offset = id - system->first_id_id;
    if (n_equations_offset == 0) {
        // nothing to do
        return 0;
    }
    if (id - system->first_id_id > system->max_equations || id > system->last_symbol_id) {
         memset_fn(&system->equations[0], 0, system->max_equations*sizeof(equation_t *));
         system->n_equations = 0;
         system->first_id_id = SYMBOL_ID_NONE;
         system->last_symbol_id = SYMBOL_ID_NONE;
         PROTOOP_PRINTF(cnx, "SET TO 0 BECAUSE ID LARGE");
    } else {
        memmove_fn(&system->equations[0], &system->equations[n_equations_offset], (system->max_equations - (n_equations_offset))*sizeof(equation_t *));
        memset_fn(&system->equations[system->max_equations - n_equations_offset], 0, n_equations_offset*sizeof(equation_t *));
        int n_non_null_equations = 0;
        for (int i = 0 ; i < system->max_equations && n_non_null_equations < system->n_equations ; i++) {
            equation_t *eq = system->equations[i];
            if (eq != NULL) {
                n_non_null_equations++;
                assert(id <= eq->pivot);
                // TODO: should we adapt eq->constant_term.first_id ? If so, we need to move the coefs accordingly, I think this is not really needed.
            }
        }
        system->first_id_id = id;
        if (n_non_null_equations == 0) {
            PROTOOP_PRINTF(cnx, "SET TO 0 BECAUSE NO NOTNULL FOUND\n");
            memset_fn(&system->equations[0], 0, system->max_equations*sizeof(equation_t *));
            system->n_equations = 0;
            system->first_id_id = SYMBOL_ID_NONE;
            system->last_symbol_id = SYMBOL_ID_NONE;
        }
    }
    return 0;
}

// id is included, so we remove it too
static __attribute__((always_inline)) int remove_all_pivots_until(picoquic_cnx_t *cnx, system_t *system, source_symbol_id_t id) {
    int n_non_null_removed = 0;
    for (source_symbol_id_t current_id = system->first_id_id ; current_id <= id ; current_id++) {
        // TODO: assert pivot OK to remove
        equation_t *removed = NULL;
        if ((removed = system_remove_pivot_for_id(cnx, system, current_id)) != NULL) {
            n_non_null_removed++;
            equation_free(cnx, removed);
        }
    }
    system->n_equations -= n_non_null_removed;
    PROTOOP_PRINTF(cnx, "removed %d equations\n", n_non_null_removed);
    if (n_non_null_removed == 0) {
        return 0;
    }
    return re_adjust_system_to(cnx, system, id+1);
}

/*---------------------------------------------------------------------------*/
static __attribute__((always_inline)) int system_add_with_elimination(picoquic_cnx_t *cnx, system_t *system,
                                equation_t *eq, uint8_t *inv_table, uint8_t **mul_table, int *decoded, equation_t **removed, int *used_in_system)
{

    *removed = NULL;
    *decoded = 0;
    *used_in_system = 0;
    int err = reduce_equation(cnx, system, eq, mul_table, inv_table);
    if (!err && !equation_is_zero(eq)) {
        uint32_t idx = system_add_as_pivot(cnx,
                system, eq, inv_table, mul_table, decoded, removed);
        PROTOOP_PRINTF(cnx, "ADDED AS PIVOT\n");
//        equation_free(fss_remove_pivot);
        if (idx  == ENTRY_INDEX_NONE || idx >= system->max_equations) {
            PROTOOP_PRINTF(cnx, "IDX TOO HIGH\n");
            return 0;
        }
        *used_in_system = 1;
        equation_t* stored_symbol
                = system->equations[idx];
        bool is_decoded = equation_has_one_id(stored_symbol);
        if (is_decoded) {
            *decoded = 1;
            source_symbol_id_t si = equation_get_min_symbol_id(stored_symbol);
            assert(equation_get_coef(stored_symbol, si)  == 1);
        }
        PROTOOP_PRINTF(cnx, "DECODED = %d\n", *decoded);
    }
    return err;
}

#endif //ONLINE_GAUSSIAN_SYSTEM_H
