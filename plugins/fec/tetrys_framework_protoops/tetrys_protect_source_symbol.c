#include "../framework/tetrys_framework_sender.c"


protoop_arg_t fec_protect_source_symbol(picoquic_cnx_t *cnx)
{
    tetrys_fec_framework_sender_t *wff = (tetrys_fec_framework_sender_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    source_symbol_t *ss = (source_symbol_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    if (ss->data_length == 0) return 0;
    int retval =  (protoop_arg_t) protect_source_symbol(cnx, wff, ss);

    if (retval == 0)
        sfpid_takes_off(wff, ss->source_fec_payload_id);
    return retval;
}