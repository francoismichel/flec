#include "../framework/tetrys_framework_sender.c"


protoop_arg_t tetrys_flush_repair_symbols(picoquic_cnx_t *cnx)
{
    tetrys_fec_framework_sender_t *ff = (tetrys_fec_framework_sender_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    return (protoop_arg_t) flush_tetrys(cnx, ff);
}