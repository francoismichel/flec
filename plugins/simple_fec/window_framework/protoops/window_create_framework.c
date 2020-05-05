#include <picoquic.h>
#include <getset.h>
#include "../../../helpers.h"
#include "../../fec_constants.h"
#include "../framework_sender.h"
#include "../framework_receiver.h"

#define RECEIVE_BUFFER_SIZE 4000

protoop_arg_t create_framework(picoquic_cnx_t *cnx)
{
    fec_scheme_t receiver_scheme = (fec_scheme_t) get_cnx(cnx, AK_CNX_INPUT, 0);
    fec_scheme_t sender_scheme = (fec_scheme_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint16_t symbol_size = (fec_scheme_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    uint64_t repair_receive_window = (fec_scheme_t) get_cnx(cnx, AK_CNX_INPUT, 3);
    window_fec_framework_t *wffs = create_framework_sender(cnx, sender_scheme);
    if (!wffs) {
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) NULL);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) NULL);
        return PICOQUIC_ERROR_MEMORY;
    }

    window_fec_framework_receiver_t *wffr = create_framework_receiver(cnx, receiver_scheme, symbol_size, repair_receive_window, RECEIVE_BUFFER_SIZE);
    if (!wffr) {
        my_free(cnx, wffs);
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) NULL);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) NULL);
        return PICOQUIC_ERROR_MEMORY;
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) wffr);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) wffs);
    return 0;
}