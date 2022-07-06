#include "../bpf.h"

#define BLOCK_STR_LEN 1200

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

protoop_arg_t protoop_log(picoquic_cnx_t *cnx) {
    TMP_FRAME_BEGIN_MALLOC(cnx, parsed_frame, frame, ack_frame_t)
        {
            size_t block_str_len = BLOCK_STR_LEN;
            char *block_str = my_malloc(cnx, block_str_len);
            if (!block_str)
                return 0;
            size_t ack_ofs = 0;
            uint64_t largest = frame->largest_acknowledged;
            int ack_block_count = frame->ack_block_count;
            for (int num_block = -1; num_block < ack_block_count; num_block++) {
                uint64_t block_to_block;
                uint64_t range;
                if (num_block == -1) {
                    range = frame->first_ack_block + 1;
                } else {
                    range = frame->ack_blocks[num_block].additional_ack_block + 1;
                }
                int written;
                ssize_t size = block_str_len - ack_ofs;
                if (range <= 1) {
                    written = snprintf(block_str + ack_ofs, size, "[\"%" PRIu64 "\"]", largest);
                    if (written >= size) {  // block_str was too small: resize it
                        ssize_t block_str_new_len = MAX(block_str_len*2, block_str_len + written);
                        char *block_str_new = my_malloc(cnx, block_str_new_len);
                        if (!block_str_new)
                            return 0;
                        my_memcpy(block_str_new, block_str, block_str_len);
                        my_free(cnx, block_str);
                        block_str = block_str_new;
                        block_str_len = block_str_new_len;
                        size = block_str_len - ack_ofs;
                        // we redo the writing, with the correct buffer size now
                        written = snprintf(block_str + ack_ofs, size, "[\"%" PRIu64 "\"]", largest);
                    }
                }
                else {
                    written = snprintf(block_str + ack_ofs, size, "[\"%" PRIu64 "\", \"%" PRIu64 "\"]", largest - range + 1, largest);
                    if (written >= size) {  // block_str was too small: resize it
                        ssize_t block_str_new_len = MAX(block_str_len*2, block_str_len + written);
                        char *block_str_new = my_malloc(cnx, block_str_new_len);
                        if (!block_str_new)
                            return 0;
                        my_memcpy(block_str_new, block_str, block_str_len);
                        my_free(cnx, block_str);
                        block_str = block_str_new;
                        block_str_len = block_str_new_len;
                        size = block_str_len - ack_ofs;
                        // we redo the writing, with the correct buffer size now
                        written = snprintf(block_str + ack_ofs, size, "[\"%" PRIu64 "\", \"%" PRIu64 "\"]", largest - range + 1, largest);
                    }
                }
                ack_ofs += written;

                ack_ofs += snprintf(block_str + ack_ofs, block_str_len - ack_ofs, num_block == ack_block_count - 1 ? "" : ", ");

                if (num_block == ack_block_count - 1)
                    break;

                block_to_block = frame->ack_blocks[num_block + 1].gap + 1;
                block_to_block += range;

                largest -= block_to_block;
            }
            block_str[ack_ofs] = 0;
            char *ack_str = my_malloc(cnx, block_str_len + 200);
            if (!ack_str)
                return 0;
            PROTOOP_SNPRINTF(cnx, ack_str, block_str_len + 200, "{\"frame_type\": \"ack\", \"ack_delay\": \"%" PRIu64 "\", \"acked_ranges\": [%s]}", frame->ack_delay, (protoop_arg_t) block_str);
            helper_log_frame(cnx, ack_str);
            my_free(cnx, block_str);
            my_free(cnx, ack_str);
        }
    TMP_FRAME_END_MALLOC(cnx, frame)
    return 0;
}