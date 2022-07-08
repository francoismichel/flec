/*
 * Copyright 2015 Big Switch Networks, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UBPF_INT_H
#define UBPF_INT_H

#include <ubpf.h>
#include "ebpf.h"

#define MAX_INSTS 65536
#define STACK_SIZE 2048 // previously 128, but it is definitely too short...

#define MAX_ERROR_MSG 200

struct ebpf_inst;

//typedef uint64_t (*ext_func)(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

typedef struct static_mem_node {
    void *ptr;
    size_t size;
    struct static_mem_node *next;
} static_mem_node_t;

struct ubpf_vm {
    struct ebpf_inst *insts;
    uint16_t num_insts;
    ubpf_jit_fn jitted;
    size_t jitted_size;
    void * (**ext_funcs) (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    const char **ext_func_names;
    static_mem_node_t *first_mem_node;
    /* If the VM crashes, indicates here why */
    char error_msg[MAX_ERROR_MSG];
};

char *ubpf_error(const char *fmt, ...);
unsigned int ubpf_lookup_registered_function(struct ubpf_vm *vm, const char *name);

#endif
