#include "picoquic.h"
#include "../fec_protoops.h"
#include "../framework/tetrys_framework.h"


/**
 * cnx->protoop_inputv[0] = uint8_t* bytes
 * cnx->protoop_inputv[1] = const uint8_t* bytes_max
 * cnx->protoop_inputv[2] = uint64_t current_time
 *
 * Output: uint8_t* bytes
 */
protoop_arg_t parse_tetrys_ack_frame(picoquic_cnx_t *cnx)
{
    PROTOOP_PRINTF(cnx, "Parse TETRYS ACK FRAME\n");
    uint8_t *bytes_protected = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    if (bytes_protected + 1 > bytes_max)
        return (protoop_arg_t) NULL;
    bytes_protected++;
    tetrys_ack_frame_t *frame = my_malloc(cnx, sizeof(tetrys_ack_frame_t));
    if (!frame)
        return (protoop_arg_t) NULL;
    my_memset(frame, 0, sizeof(tetrys_ack_frame_t));
    if (parse_tetrys_ack_frame_header(&frame->header, bytes_protected, bytes_max)) {
        my_free(cnx, frame);
        PROTOOP_PRINTF(cnx, "ERROR WHEN PARSING TETRYS_ACK_FRAME HEADER\n");
        return (protoop_arg_t) NULL;
    }
    bytes_protected += sizeof(tetrys_ack_frame_header_t);
    if (bytes_protected + frame->header.length > bytes_max) {
        my_free(cnx, frame);
        return (protoop_arg_t) NULL;
    }
    frame->data = my_malloc(cnx, frame->header.length);
    if (!frame->data) {
        my_free(cnx, frame);
        return (protoop_arg_t) NULL;
    }
    my_memcpy(frame->data, bytes_protected, frame->header.length);
    bytes_protected += frame->header.length;
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) frame);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) true);
    set_cnx(cnx, AK_CNX_OUTPUT, 2, (protoop_arg_t) false);
    return (protoop_arg_t) bytes_protected;
}