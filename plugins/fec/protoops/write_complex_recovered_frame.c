#include <picoquic.h>
#include "../fec_protoops.h"


/*
 * The format of a RECOVERED frame is:
 *
 * 1. type byte (8bits)
 * 2. number of recovered packets (8bits)
 * 3. first recovered packet full-size packet number (64bits)
 * 4. recovered packets range (recovered packets with consecutive packet number comapred to the first packet or the last gap)
 * 5. recovered packets gap (non-recovered packets with consecutive packet number comapred to the last recovered range)
 *   (repeat (4,5) as many times as needed)
 * [6. last recovered range if there are more than one recovered packet after the last gap]
 */


protoop_arg_t write_recovered_frame(picoquic_cnx_t *cnx) {
    uint8_t* bytes = (uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    const uint8_t* bytes_max = (const uint8_t *) get_cnx(cnx, AK_CNX_INPUT, 1);
    recovered_packets_t *rp = (recovered_packets_t *) get_cnx(cnx, AK_CNX_INPUT, 2);
    PROTOOP_PRINTF(cnx, "WRITE RECOVERED FRAME, %d PACKETS, %d SFPIDs\n", rp->number_of_packets, rp->number_of_sfpids);
    int consumed = 0;
    if (rp->number_of_packets == 0 || bytes_max - bytes < 2*sizeof(uint8_t) + sizeof(uint64_t)) {
        set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) consumed);
        set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
        free_rp(cnx, rp);
        return -1;
    }

    // the packets in rp must be sorted according to their packet number
    my_memset(bytes, RECOVERED_TYPE, sizeof(uint8_t));
    bytes += sizeof(uint8_t);
    consumed += sizeof(uint8_t);

    ssize_t size = 0;

    size = picoquic_varint_encode(bytes, bytes_max - bytes, rp->number_of_packets);
    bytes += size;
    consumed += size;

    size = picoquic_varint_encode(bytes, bytes_max - bytes, rp->number_of_sfpids);
    bytes += size;
    consumed += size;

    encode_u64(rp->packets[0], bytes);
    bytes += sizeof(uint64_t);
    consumed += sizeof(uint64_t);

    uint64_t range_length = 0;
    PROTOOP_PRINTF(cnx, "FIRST PACKET ID %u\n", rp->packets[0]);
    for (int i = 1 ; i < rp->number_of_packets ; i++) {
        PROTOOP_PRINTF(cnx, "WRITE PACKET %u\n", rp->packets[i]);
        // FIXME: handle gaps of more than 0xFF
        if (rp->packets[i] <= rp->packets[i-1]) {
            // error
            set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 0);
            set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
            PROTOOP_PRINTF(cnx, "ERROR: THE PACKETS ARE NOT IN ORDER IN THE RECOVERED FRAME\n");
            free_rp(cnx, rp);
            return -1;
        }
        if (rp->packets[i] == rp->packets[i-1]) {
            range_length++;
        } else {
            if (bytes_max - bytes < 2*sizeof(uint8_t)) {
                set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 0);
                set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
                PROTOOP_PRINTF(cnx, "ERROR TOO FEW AVAILABLE BYTES\n");
                free_rp(cnx, rp);
                return -1;
            }
            // write range
            size = picoquic_varint_encode(bytes, bytes_max - bytes, range_length);
            bytes += size;
            consumed += size;
            PROTOOP_PRINTF(cnx, "WRITE RANGE %lu\n", range_length);
            // write gap
            size = picoquic_varint_encode(bytes, bytes_max - bytes, (rp->packets[i] - rp->packets[i-1]) - 1);
            bytes += size;
            consumed += size;
            PROTOOP_PRINTF(cnx, "WRITE GAP RANGE %lu\n", (rp->packets[i] - rp->packets[i-1]));
            range_length = 0;
        }
    }
    // write last range if needed
    if (range_length > 0) {
        if (bytes_max - bytes < 2*sizeof(uint8_t)) {
            set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) 0);
            set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 0);
            PROTOOP_PRINTF(cnx, "ERROR TOO FEW AVAILABLE BYTES 2\n");
            free_rp(cnx, rp);
            return -1;
        }
        size = picoquic_varint_encode(bytes, bytes_max - bytes, range_length);
        bytes += size;
        consumed += size;
    }
    // bulk-encode the sfpids
    for (int i = 0 ; i < rp->number_of_sfpids ; i++) {
        PROTOOP_PRINTF(cnx, "WRITE RECOVERED SFPID %u\n", rp->recovered_sfpids[i].raw);
        encode_u32(rp->recovered_sfpids[i].raw, bytes);
        consumed += sizeof(uint32_t);
        bytes += sizeof(uint32_t);
    }
    set_cnx(cnx, AK_CNX_OUTPUT, 0, (protoop_arg_t) consumed);
    set_cnx(cnx, AK_CNX_OUTPUT, 1, (protoop_arg_t) 1);
    // rp will be freed by notify_frame, to let the opportunity to retransmit the frame
    return 0;
}