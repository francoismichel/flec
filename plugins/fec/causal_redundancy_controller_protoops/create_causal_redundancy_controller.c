#include "picoquic.h"
#include "causal_redundancy_controller.h"
#include "../fec_protoops.h"

// sets as output the pointer towards the controller's state
protoop_arg_t create_dynamic_uniform_redundancy_controller(picoquic_cnx_t *cnx)
{
    causal_redundancy_controller_t *c = create_causal_redundancy_controller(cnx, 0, 1, 20);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) c);
    return 0;
}