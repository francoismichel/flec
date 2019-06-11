#include "../framework/tetrys_framework_sender.c"


protoop_arg_t fec_protect_source_symbol(picoquic_cnx_t *cnx)
{
    tetrys_fec_framework_t *wff = (tetrys_fec_framework_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    source_symbol_t *ss = (source_symbol_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    if (ss->data_length == 0) return 0;
    return (protoop_arg_t) protect_source_symbol(cnx, wff, ss);
}