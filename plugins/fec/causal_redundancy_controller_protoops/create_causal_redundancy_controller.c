#include "picoquic.h"
#include "causal_redundancy_controller.h"
#include "../fec_protoops.h"

// sets as output the pointer towards the controller's state
protoop_arg_t create_dynamic_uniform_redundancy_controller(picoquic_cnx_t *cnx)
{
    dynamic_uniform_redundancy_controller_t *urc = my_malloc(cnx, sizeof(dynamic_uniform_redundancy_controller_t));
    if (!urc) {
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) NULL);
        return PICOQUIC_ERROR_MEMORY;
    }
    my_memset(urc, 0, sizeof(dynamic_uniform_redundancy_controller_t));
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) urc);
    return 0;
}