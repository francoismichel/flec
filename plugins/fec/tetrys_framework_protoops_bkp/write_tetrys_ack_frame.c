#include <picoquic.h>
#include "../fec_protoops.h"
#include "../framework/tetrys_framework.h"

protoop_arg_t write_tetrys_ack_frame(picoquic_cnx_t *cnx) {
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    tetrys_ack_frame_t *tf = (tetrys_ack_frame_t *) get_cnx(cnx, AK_CNX_INPUT, 2);
    if (bytes + 1 + sizeof(tetrys_ack_frame_header_t) + tf->header.length > bytes_max) {
        PROTOOP_PRINTF(cnx, "COULDN'T WRITE TETRYS ACK FRAME: %p > %p\n", (protoop_arg_t) bytes + 1 + sizeof(tetrys_ack_frame_header_t) + tf->header.length,
                       (protoop_arg_t) bytes_max);
        return -1;
    }
    size_t consumed = write_tetrys_ack_frame_header(cnx, &tf->header, bytes);
    if (bytes + consumed + tf->header.length > bytes_max) {
        PROTOOP_PRINTF(cnx, "COULDN'T WRITE TETRYS ACK FRAME AFTER WRITING HEADER: %p > %p\n", (protoop_arg_t) bytes + consumed + tf->header.length,
                       (protoop_arg_t) bytes_max);
        return -1;
    }
    my_memcpy(bytes + consumed, tf->data, tf->header.length);
    consumed += tf->header.length;
    my_free(cnx, tf->data);
    my_free(cnx, tf);
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
    return 0;
}