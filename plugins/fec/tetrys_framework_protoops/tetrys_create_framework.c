#include "../framework/tetrys_framework_sender.c"
#include "../framework/tetrys_framework_receiver.c"
#include "../fec_scheme_protoops/rlc_fec_scheme_gf256.h"


protoop_arg_t create_framework(picoquic_cnx_t *cnx) {
    tetrys_fec_framework_t *ff = tetrys_create_framework(cnx);
    if (!ff) {
        my_free(cnx, ff);
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) NULL);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) NULL);
        return PICOQUIC_ERROR_MEMORY;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) ff);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) ff);
    return 0;
}