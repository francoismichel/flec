#ifndef PICOQUIC_RED_BLACK_TREE_H
#define PICOQUIC_RED_BLACK_TREE_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <picoquic.h>
#include "../../helpers.h"
#include "types.h"

/**
 * Highly inspired from Algorithms, Fourth edition, https://algs4.cs.princeton.edu/33balanced/RedBlackBST.java
 */


#define RBT_MAX_DEPTH 70

#define RED true
#define BLACK false



typedef uint64_t rbt_key;
typedef void * rbt_val;

// BST helper node data type
typedef struct rbt_node {
    rbt_key key;           // key
    rbt_val val;           // associated data
    struct rbt_node *left;
    struct rbt_node *right;  // links to left and right subtrees
    bool   color;     // color of parent link
    int size;          // subtree count
} rbt_node_t;

typedef struct {
    rbt_node_t *node;
    bool right;
} rbt_node_path_t;

typedef struct {
    rbt_node_path_t nodes[RBT_MAX_DEPTH];
    int current_size;
} rbt_array_stack_t;

static __attribute__((always_inline)) int rbt_stack_init(rbt_array_stack_t *stack) {
    my_memset(stack, 0, sizeof(rbt_array_stack_t));
    return 0;
}

static __attribute__((always_inline)) int rbt_stack_size(rbt_array_stack_t *stack) {
    return stack->current_size;
}

static __attribute__((always_inline)) int rbt_stack_push(rbt_array_stack_t *stack, rbt_node_t *node, bool right) {
    if (!node || stack->current_size == RBT_MAX_DEPTH)
        return -1;
    stack->nodes[stack->current_size++] = (rbt_node_path_t) { .node = node, .right = right };
    return 0;
}

static __attribute__((always_inline)) rbt_node_path_t rbt_stack_pop(rbt_array_stack_t *stack) {
    if (stack->current_size == 0)
        return (rbt_node_path_t) { .node = NULL };
    return stack->nodes[--stack->current_size];
}

typedef struct {
    rbt_node_t *root;
    rbt_array_stack_t helper_stack;    // to simulate recursion
    rbt_array_stack_t second_helper_stack;    // to simulate recursion
} red_black_tree_t;

static __attribute__((always_inline)) int rbt_init(red_black_tree_t *tree) {
    my_memset(tree, 0, sizeof(red_black_tree_t));
    int ret = rbt_stack_init(&tree->helper_stack);
    if (ret == 0) {
        ret = rbt_stack_init(&tree->second_helper_stack);
    }
    return ret;
}

static __attribute__((always_inline)) rbt_node_t *rbt_new_node(picoquic_cnx_t *cnx, rbt_key key, rbt_val val, bool color, int size) {
    rbt_node_t *newnode = my_malloc(cnx, sizeof(rbt_node_t));
    if (!newnode) return NULL;
    my_memset(newnode, 0, sizeof(rbt_node_t));
    newnode->key = key;
    newnode->val = val;
    newnode->color = color;
    newnode->size = size;
    return newnode;
}

static __attribute__((always_inline)) int rbt_destroy_node(picoquic_cnx_t *cnx, rbt_node_t *node) {
    if (!node) return 0;
    my_free(cnx, node);
    return 0;
}


/***************************************************************************
 *  Node helper methods.
 ***************************************************************************/
// is node x red; false if x is null ?
static __attribute__((always_inline)) bool is_red(rbt_node_t *x) {
    if (x == NULL) return false;
    return x->color == RED;
}

// number of node in subtree rooted at x; 0 if x is null
static __attribute__((always_inline)) int rbt_node_size(rbt_node_t *x) {
    if (x == NULL) return 0;
    return x->size;
}


/**
 * Returns the number of key-value pairs in this symbol table.
 * @return the number of key-value pairs in this symbol table
 */
static __attribute__((always_inline)) int rbt_size(red_black_tree_t *tree) {
    return rbt_node_size(tree->root);
}

/**
  * Is this symbol table empty?
  * @return {@code true} if this symbol table is empty and {@code false} otherwise
  */
static __attribute__((always_inline)) bool rbt_is_empty(red_black_tree_t *tree) {
    return tree->root == NULL;
}

static __attribute__((always_inline)) int key_compare(rbt_key a, rbt_key b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}



/***************************************************************************
 *  Standard BST search.
 ***************************************************************************/


// value associated with the given key in subtree rooted at x; null if no such key
static __attribute__((always_inline)) rbt_val _rbt_get(rbt_node_t *x, rbt_key key) {
    while (x != NULL) {
        int cmp = key_compare(key, x->key);
        if      (cmp < 0) x = x->left;
        else if (cmp > 0) x = x->right;
        else              return x->val;
    }
    return NULL;
}

/**
 * Returns the value associated with the given key.
 * @param key the key
 * @return the value associated with the given key if the key is in the symbol table
 *     and {@code null} if the key is not in the symbol table
 * @throws IllegalArgumentException if {@code key} is {@code null}
 */
static __attribute__((always_inline)) rbt_val rbt_get(red_black_tree_t *tree, rbt_key key) {
    return _rbt_get(tree->root, key);
}


/**
 * Does the RBT contain the given key?
 * @param key the key
 * @return {@code true} if this symbol table contains {@code key} and
 *     {@code false} otherwise
 * @throws IllegalArgumentException if {@code key} is {@code null}
 */
static __attribute__((always_inline)) bool rbt_contains(red_black_tree_t *tree, rbt_key key) {
    return rbt_get(tree, key) != NULL;
}



/***************************************************************************
 *  Red-black tree helper functions.
 ***************************************************************************/

// make a left-leaning link lean to the right
static __attribute__((always_inline)) rbt_node_t *rbt_rotate_right(rbt_node_t *h) {
    // assert (h != null) && isRed(h.left);
    rbt_node_t *x = h->left;
    h->left = x->right;
    x->right = h;
    x->color = x->right->color;
    x->right->color = RED;
    x->size = h->size;
    h->size = rbt_node_size(h->left) + rbt_node_size(h->right) + 1;
    return x;
}

// make a right-leaning link lean to the left
static __attribute__((always_inline)) rbt_node_t *rbt_rotate_left(rbt_node_t *h) {
// assert (h != null) && isRed(h.right);
    rbt_node_t *x = h->right;
    h->right = x->left;
    x->left = h;
    x->color = x->left->color;
    x->left->color = RED;
    x->size = h->size;
    h->size = rbt_node_size(h->left) + rbt_node_size(h->right) + 1;
    return x;
}

// flip the colors of a node and its two children
static __attribute__((always_inline)) void rbt_flip_colors(rbt_node_t *h) {
    // h must have opposite color of its two children
    // assert (h != null) && (h.left != null) && (h.right != null);
    // assert (!isRed(h) &&  isRed(h.left) &&  isRed(h.right))
    //    || (isRed(h)  && !isRed(h.left) && !isRed(h.right));
    h->color = !h->color;
    h->left->color = !h->left->color;
    h->right->color = !h->right->color;
}

// Assuming that h is red and both h.left and h.left.left
// are black, make h.left or one of its children red.
static __attribute__((always_inline)) rbt_node_t *rbt_move_red_left(rbt_node_t *h) {
// assert (h != null);
// assert isRed(h) && !isRed(h.left) && !isRed(h.left.left);

    rbt_flip_colors(h);
    if (is_red(h->right->left)) {
        h->right = rbt_rotate_right(h->right);
        h = rbt_rotate_left(h);
        rbt_flip_colors(h);
    }
    return h;
}

// Assuming that h is red and both h.right and h.right.left
// are black, make h.right or one of its children red.
static __attribute__((always_inline)) rbt_node_t *rbt_move_red_right(rbt_node_t *h) {
// assert (h != null);
// assert isRed(h) && !isRed(h.right) && !isRed(h.right.left);
    rbt_flip_colors(h);
    if (is_red(h->left->left)) {
        h = rbt_rotate_right(h);
        rbt_flip_colors(h);
    }
    return h;
}

// restore red-black tree invariant
static __attribute__((always_inline)) rbt_node_t *rbt_balance(rbt_node_t *h) {
// assert (h != null);

    if (is_red(h->right))                      h = rbt_rotate_left(h);
    if (is_red(h->left) && is_red(h->left->left)) h = rbt_rotate_right(h);
    if (is_red(h->left) && is_red(h->right)) rbt_flip_colors(h);

    h->size = rbt_node_size(h->left) + rbt_node_size(h->right) + 1;
    return h;
}




/***************************************************************************
 *  Red-black tree insertion.
 ***************************************************************************/

/**
 * Inserts the specified key-value pair into the symbol table, overwriting the old
 * value with the new value if the symbol table already contains the specified key.
 * Deletes the specified key (and its associated value) from this symbol table
 * if the specified value is {@code null}.
 *
 * @param key the key
 * @param val the value
 * @throws IllegalArgumentException if {@code key} is {@code null}
 */


// insert the key-value pair in the subtree rooted at h
static __attribute__((always_inline)) rbt_node_t *rbt_node_put(picoquic_cnx_t *cnx, rbt_array_stack_t *stack, rbt_node_t *node, rbt_key key, rbt_val val) {

    rbt_node_t *current_node = node;

    PROTOOP_PRINTF(cnx, "PUT, FIRST PART\n");
    while(current_node) {
        PROTOOP_PRINTF(cnx, "WHILE LOOP, CURRENT = %p\n", (protoop_arg_t) current_node);
        int cmp = key_compare(key, current_node->key);
        if      (cmp < 0)  {
            // continue to search
            PROTOOP_PRINTF(cnx, "BEFORE STACK PUSH 1\n");
            rbt_stack_push(stack, current_node, false);
            PROTOOP_PRINTF(cnx, "AFTER STACK PUSH 1\n");
            current_node = current_node->left;  //current_node->left  = put(cnx, current_node->left,  key, val);
        }
        else if (cmp > 0) {
            // continue to search
            PROTOOP_PRINTF(cnx, "BEFORE STACK PUSH 2\n");
            rbt_stack_push(stack, current_node, true);
            PROTOOP_PRINTF(cnx, "AFTER STACK PUSH 2 !\n");
            current_node = current_node->right;  //current_node->right  = put(cnx, current_node->right,  key, val);
            PROTOOP_PRINTF(cnx, "AFTER NEXT\n");
        }
        else {
            current_node->val   = val;
            break;
        }
    }
    PROTOOP_PRINTF(cnx, "DONE WHILE, current = %p\n", (protoop_arg_t) current_node);
    if (!current_node) {
        PROTOOP_PRINTF(cnx, "CREATE NEW NODE\n");
        current_node = rbt_new_node(cnx, key, val, RED, 1);
        if (!current_node) {
            PROTOOP_PRINTF(cnx, "Out of memory when creating a new node\n");
        }
    }

    PROTOOP_PRINTF(cnx, "PUT, SECOND PART\n");

    // from this, the current node  subtree has the correct colors but we need the fix the path from the root to this node

    rbt_node_path_t path_from_parent;
    rbt_node_t *parent = NULL;
    // fix the colors by simulating the recursion
    while(rbt_stack_size(stack) > 0) {
        path_from_parent = rbt_stack_pop(stack);
        parent = path_from_parent.node;
        // update the parent's changed child
        if   (path_from_parent.right) parent->right = current_node;
        else                          parent->left  = current_node;
        // fix-up any right-leaning links
        if (is_red(parent->right) && !is_red(parent->left))      parent = rbt_rotate_left(parent);
        if (is_red(parent->left)  &&  is_red(parent->left->left)) parent = rbt_rotate_right(parent);
        if (is_red(parent->left)  &&  is_red(parent->right)) rbt_flip_colors(parent);
        parent->size = rbt_node_size(parent->left) + rbt_node_size(parent->right) + 1;
        current_node = parent;  // ready for next iteration
    }

    // current_node is now the new root of the subtree rooted at node
    return current_node;
}

static __attribute__((always_inline)) void rbt_put(picoquic_cnx_t *cnx, red_black_tree_t *tree, rbt_key key, rbt_val val) {
    PROTOOP_PRINTF(cnx, "PUT KEY %lu IN TREE %p\n", key, (protoop_arg_t) tree);
    tree->root = rbt_node_put(cnx, &tree->helper_stack, tree->root, key, val);
    tree->root->color = BLACK;
    // assert check();
}










/***************************************************************************
 *  Ordered symbol table methods.
 ***************************************************************************/


// the smallest key in subtree rooted at x; null if no such key
static __attribute__((always_inline)) rbt_node_t *rbt_node_min(rbt_node_t *node) {
    rbt_node_t *current_node = node;
    while(current_node) {
        // assert x != null;
        if (current_node->left == NULL) return current_node;
        else                            current_node = current_node->left;
    }
    return NULL;
}

/**
 * pre: tree not empty
 * Returns the smallest key in the symbol table.
 * @return the smallest key in the symbol table
 */
static __attribute__((always_inline)) rbt_key rbt_min_key(red_black_tree_t *tree) {
    return rbt_node_min(tree->root)->key;
}

/**
 * pre: tree not empty
 * Returns the val of the smallest key in the symbol table.
 * @return the val of the smallest key in the symbol table
 */
static __attribute__((always_inline)) rbt_val rbt_min_val(red_black_tree_t *tree) {
    return rbt_node_min(tree->root)->val;
}

// the largest key in subtree rooted at x; null if no such key
static __attribute__((always_inline)) rbt_node_t *rbt_node_max(rbt_node_t *node) {
    rbt_node_t *current_node = node;
    while(current_node) {
        // assert x != null;
        if (current_node->right == NULL) return current_node;
        else                            current_node = current_node->right;
    }
    return NULL;
}

/**
 * pre: tree not empty
 * Returns the largest key in the symbol table.
 * @return the largest key in the symbol table
 */
static __attribute__((always_inline)) rbt_key rbt_max_key(red_black_tree_t *tree) {
    return rbt_node_max(tree->root)->key;
}

/**
 * pre: tree not empty
 * Returns the val of the largest key in the symbol table.
 * @return the val of the largest key in the symbol table
 */
static __attribute__((always_inline)) rbt_val rbt_max_val(red_black_tree_t *tree) {
    return rbt_node_max(tree->root)->val;
}






// needed to make the while loop in rbt_node_delete_min compile with clang6 (not needed in clang9)
static __attribute__((always_inline)) bool is_not_null(void *ptr) {
    return (((uint32_t) ptr) | ((uint32_t) (((uint64_t)ptr) >> 32U))) != 0;
}


/***************************************************************************
 *  Red-black tree deletion.
 ***************************************************************************/


// delete the key-value pair with the minimum key rooted at h
static __attribute__((always_inline)) rbt_node_t *rbt_node_delete_min(picoquic_cnx_t *cnx, rbt_array_stack_t *stack, rbt_node_t *node) {
    rbt_node_t *current_node = node;


    while(is_not_null(current_node)) {
        if (current_node->left == NULL) {   // this is the smallest, let's remove it
            // destroy the node and release memory
            rbt_destroy_node(cnx, current_node);
            // !!! if the value is something malloc'd, it will be lost !
            current_node = NULL;
            break;
        }
        if (!is_red(current_node->left) && !is_red(current_node->left->left)) {
            current_node = rbt_move_red_left(current_node);
        }
        // search left
        rbt_stack_push(stack, current_node, false); // simulate recursion stack
        current_node = current_node->left;  // recurse
    }

    rbt_node_path_t path_from_parent;
    rbt_node_t *parent = NULL;
    while(rbt_stack_size(stack) > 0) {
        path_from_parent = rbt_stack_pop(stack);
        parent = path_from_parent.node;
        // update the parent's changed child
        if   (path_from_parent.right) parent->right = current_node;
        else                          parent->left  = current_node;
        parent = rbt_balance(parent);
        current_node = parent;
    }


    return current_node;
}



/**
 * Removes the smallest key and associated value from the symbol table.
 * @throws NoSuchElementException if the symbol table is empty
 */
static __attribute__((always_inline)) void rbt_delete_min(picoquic_cnx_t *cnx, red_black_tree_t *tree) {
    // if both children of root are black, set root to red
    if (!is_red(tree->root->left) && !is_red(tree->root->right))
        tree->root->color = RED;

    tree->root = rbt_node_delete_min(cnx, &tree->helper_stack, tree->root);
    if (!rbt_is_empty(tree)) tree->root->color = BLACK;
    // assert check();
}


// delete the key-value pair with the maximum key rooted at h
static __attribute__((always_inline)) rbt_node_t *rbt_node_delete_max(picoquic_cnx_t *cnx, rbt_array_stack_t *stack, rbt_node_t *node) {
    rbt_node_t *current_node = node;
    while(current_node) {
        if (is_red(current_node->left))
            current_node = rbt_rotate_right(current_node);

        if (current_node->right == NULL) {  // we found the largest, let's remove it
            // destroy the node and release memory
            rbt_destroy_node(cnx, current_node);
            // !!! if the value is something malloc'd, it will be lost !
            current_node = NULL;
            break;
        }
        if (!is_red(current_node->right) && !is_red(current_node->right->left)){
            current_node = rbt_move_red_right(current_node);
        }
        // search right
        rbt_stack_push(stack, current_node, true);  // simulate resursion stack
        current_node = current_node->right;         // recurse
    }


    // re-balance the tree
    rbt_node_path_t path_from_parent;
    rbt_node_t *parent = NULL;
    while(rbt_stack_size(stack) > 0) {
        path_from_parent = rbt_stack_pop(stack);
        parent = path_from_parent.node;
        // update the parent's changed child
        if   (path_from_parent.right) parent->right = current_node;
        else                          parent->left  = current_node;
        parent = rbt_balance(parent);
        current_node = parent;
    }


    return current_node;
}

/**
 * Removes the largest key and associated value from the symbol table.
 * @throws NoSuchElementException if the symbol table is empty
 */
static __attribute__((always_inline)) void rbt_delete_max(picoquic_cnx_t *cnx, red_black_tree_t *tree) {

    // if both children of root are black, set root to red
    if (!is_red(tree->root->left) && !is_red(tree->root->right))
        tree->root->color = RED;

    tree->root = rbt_node_delete_max(cnx, &tree->helper_stack, tree->root);
    if (!rbt_is_empty(tree)) tree->root->color = BLACK;
    // assert check();
}

// delete the key-value pair with the given key rooted at h
static __attribute__((always_inline)) rbt_node_t *rbt_node_delete(picoquic_cnx_t *cnx, rbt_array_stack_t *stack, rbt_array_stack_t *second_stack, rbt_node_t *node, rbt_key key) {
// assert get(h, key) != null;

    rbt_node_t *current_node = node;

    while(current_node) {
        if (key_compare(key, current_node->key) < 0)  {
            if (!is_red(current_node->left) && !is_red(current_node->left->left))
                current_node = rbt_move_red_left(current_node);
            // search left
            rbt_stack_push(stack, current_node, false); // prepare for recursion
            current_node = current_node->left;          // recurse
        }
        else {
            if (is_red(current_node->left))
                current_node = rbt_rotate_right(current_node);
            if (key_compare(key, current_node->key) == 0 && (current_node->right == NULL)) {  // we found the node and the right child is NULL, let's remove it
                // destroy the node and release memory
                rbt_destroy_node(cnx, current_node);
                // !!! if the value is something malloc'd, it will be lost !
                current_node = NULL;
                break;
            }
            if (!is_red(current_node->right) && !is_red(current_node->right->left))
                current_node = rbt_move_red_right(current_node);
            if (key_compare(key, current_node->key) == 0) {
                rbt_node_t *x = rbt_node_min(current_node->right);
                current_node->key = x->key;
                current_node->val = x->val;
                current_node->right = rbt_node_delete_min(cnx, second_stack, current_node->right);
                // this below is needed to readjust correctly the size after the removal
                rbt_stack_push(stack, current_node, true);
                current_node = current_node->right;
                break;
            }
            else {
                // search right
                rbt_stack_push(stack, current_node, true);
                current_node = current_node->right;
            }
        }
    }


    // re-balance the tree
    rbt_node_path_t path_from_parent;
    rbt_node_t *parent = NULL;
    while(rbt_stack_size(stack) > 0) {
        path_from_parent = rbt_stack_pop(stack);
        parent = path_from_parent.node;
        // update the parent's changed child
        if   (path_from_parent.right) {
            parent->right = current_node;
        }
        else                          {
            parent->left  = current_node;
        }
        parent = rbt_balance(parent);
        current_node = parent;
    }
    return current_node;
}



// the smallest key in the subtree rooted at x greater than or equal to the given key
static __attribute__((always_inline)) bool rbt_node_ceiling(rbt_array_stack_t *stack, rbt_node_t *node, rbt_key key,
                                                                rbt_key *out_key, rbt_val *out_val) {
    rbt_stack_push(stack, node, false);
    rbt_node_t *x = NULL;
    rbt_node_path_t last_path;
    rbt_key current_ceiling = (rbt_key) UINT64_MAX;
    rbt_val current_val = 0;
    bool found = false;
    while(rbt_stack_size(stack) > 0) {
        last_path = rbt_stack_pop(stack);
        x = last_path.node;
        if (x) {
            int cmp = key_compare(key, x->key);
            if (cmp == 0) {
                current_ceiling = x->key;
                current_val = x->val;
                found = true;
                break;
            } else if (cmp > 0) {
                rbt_stack_push(stack, x->right, true);
            } else {    // x->key is greater than key, so it is a ceiling candidate
                found = true;
                if (key_compare(x->key, current_ceiling) < 0) {
                    current_ceiling = x->key;
                    current_val = x->val;
                }
                rbt_stack_push(stack, x->left, false);
            }
        }
    }
    if (out_key != NULL)
        *out_key = current_ceiling;
    if (out_val != NULL)
        *out_val = current_val;
    return found;
}


/**
     * @pre: the tree must not be empty
     * Returns the smallest key in the symbol table greater than or equal to {@code key}.
     * @param key the key
     * @return the smallest key in the symbol table greater than or equal to {@code key}
     */
static __attribute__((always_inline)) bool rbt_ceiling(red_black_tree_t *tree, rbt_key key, rbt_key *out_key, rbt_val *out_val) {
    if (rbt_is_empty(tree))
        return false;
    return rbt_node_ceiling(&tree->helper_stack, tree->root, key, out_key, out_val);
}

/**
     * @pre: the tree must not be empty
     * Returns the smallest key in the symbol table greater than or equal to {@code key}.
     * @param key the key
     * @return the smallest key in the symbol table greater than or equal to {@code key}
     */
static __attribute__((always_inline)) bool rbt_ceiling_key(red_black_tree_t *tree, rbt_key key, rbt_key *out_key) {
    if (rbt_is_empty(tree))
        return false;
    return rbt_node_ceiling(&tree->helper_stack, tree->root, key, out_key, NULL);
}
/**
     * @pre: the tree must not be empty
     * Returns the smallest key in the symbol table greater than or equal to {@code key}.
     * @param key the key
     * @return the smallest key in the symbol table greater than or equal to {@code key}
     */
static __attribute__((always_inline)) bool rbt_ceiling_val(red_black_tree_t *tree, rbt_key key, rbt_val *out_val) {
    if (rbt_is_empty(tree))
        return false;
    return rbt_node_ceiling(&tree->helper_stack, tree->root, key, NULL, out_val);
}

/**
 * Removes the specified key and its associated value from this symbol table
 * (if the key is in this symbol table).
 *
 * @param  key the key
 * @throws IllegalArgumentException if {@code key} is {@code null}
 */
static __attribute__((always_inline)) void rbt_delete(picoquic_cnx_t *cnx, red_black_tree_t *tree, rbt_key key) {
    if (!rbt_contains(tree, key)) return;

    // if both children of root are black, set root to red
    if (!is_red(tree->root->left) && !is_red(tree->root->right))
        tree->root->color = RED;

    tree->root = rbt_node_delete(cnx, &tree->helper_stack, &tree->second_helper_stack, tree->root, key);
    if (!rbt_is_empty(tree)) tree->root->color = BLACK;
    // assert check();
}



#endif //PICOQUIC_RED_BLACK_TREE_H
