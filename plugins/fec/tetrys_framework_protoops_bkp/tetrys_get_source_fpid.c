#include "../framework/tetrys_framework_sender.c"


protoop_arg_t tetrys_get_source_fpid(picoquic_cnx_t *cnx)
{
    tetrys_fec_framework_t *wff = (tetrys_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    return (protoop_arg_t) get_source_fpid(wff).raw;
}