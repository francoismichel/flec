/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
* Packet logging.
*/
#include <stdio.h>
#include <string.h>
#include "fnv1a.h"
#include "picoquic_internal.h"
#include "tls_api.h"

void picoquic_log_bytes(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    for (size_t i = 0; i < bytes_max;) {
        fprintf(F, "%04x:  ", (int)i);

        for (int j = 0; j < 16 && i < bytes_max; j++, i++) {
            fprintf(F, "%02x ", bytes[i]);
        }
        fprintf(F, "\n");
    }
}

void picoquic_log_error_packet(FILE* F, uint8_t* bytes, size_t bytes_max, int ret)
{
    fprintf(F, "Packet length %d caused error: %d\n", (int)bytes_max, ret);

    picoquic_log_bytes(F, bytes, bytes_max);

    fprintf(F, "\n");
}

void picoquic_log_time(FILE* F, picoquic_cnx_t* cnx, uint64_t current_time,
    const char* label1, const char* label2)
{
    uint64_t delta_t = (cnx == NULL) ? current_time : current_time - cnx->start_time;
    uint64_t time_sec = delta_t / 1000000;
    uint32_t time_usec = (uint32_t)(delta_t % 1000000);

    fprintf(F, "%s%" PRIu64 ".%06d%s", label1,
        time_sec, time_usec, label2);
}

const char * picoquic_log_fin_or_event_name(picoquic_call_back_event_t ev)
{
    char const * text = "unknown";
    switch (ev) {
    case picoquic_callback_no_event:
        text = "no event";
        break;
    case picoquic_callback_stream_fin:
        text = "stream fin";
        break;
    case picoquic_callback_stream_reset:
        text = "stream reset";
        break;
    case picoquic_callback_stop_sending:
        text = "stop sending";
        break;
    case picoquic_callback_close:
        text = "connection close";
        break;
    case picoquic_callback_application_close:
        text = "application close";
        break;
    case picoquic_callback_stream_gap:
        text = "stream gap";
        break;
    case picoquic_callback_prepare_to_send:
        text = "ready to send";
        break;
    case picoquic_callback_almost_ready:
        text = "almost ready";
        break;
    case picoquic_callback_ready:
        text = "ready";
        break;
    default:
        break;
    }

    return text;
}

void picoquic_log_packet_address(FILE* F, uint64_t log_cnxid64, picoquic_cnx_t* cnx,
    struct sockaddr* addr_peer, int receiving, size_t length, uint64_t current_time)
{
    uint64_t delta_t = 0;
    uint64_t time_sec = 0;
    uint32_t time_usec = 0;

    if (!F) {
        return;
    }

    if (log_cnxid64 != 0) {
        fprintf(F, "%" PRIx64 ": ", log_cnxid64);
    }

    fprintf(F, (receiving) ? "Receiving %d bytes from " : "Sending %d bytes to ",
        (int)length);

    if (addr_peer->sa_family == AF_INET) {
        struct sockaddr_in* s4 = (struct sockaddr_in*)addr_peer;
        uint8_t* addr = (uint8_t*)&s4->sin_addr;

        fprintf(F, "%d.%d.%d.%d:%d",
            addr[0], addr[1], addr[2], addr[3],
            ntohs(s4->sin_port));
    } else {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)addr_peer;
        uint8_t* addr = (uint8_t*)&s6->sin6_addr;

        for (int i = 0; i < 8; i++) {
            if (i != 0) {
                fprintf(F, ":");
            }

            if (addr[2 * i] != 0) {
                fprintf(F, "%x%02x", addr[2 * i], addr[(2 * i) + 1]);
            } else {
                fprintf(F, "%x", addr[(2 * i) + 1]);
            }
        }
    }

    if (cnx != NULL) {
        delta_t = current_time - cnx->start_time;
        time_sec = delta_t / 1000000;
        time_usec = (uint32_t)(delta_t % 1000000);
    }

    fprintf(F, " at T=%" PRIu64 ".%06d (%" PRIx64 ")\n",
        time_sec, time_usec,
        current_time);
}

char const* picoquic_log_state_name(picoquic_state_enum state)
{
    char const* state_name = "unknown";

    switch (state) {
    case picoquic_state_client_init:
        state_name = "client_init";
        break;
    case picoquic_state_client_init_sent:
        state_name = "client_init_sent";
        break;
    case picoquic_state_client_renegotiate:
        state_name = "client_renegotiate";
        break;
    case picoquic_state_client_retry_received:
        state_name = "client_retry_received";
        break;
    case picoquic_state_client_init_resent:
        state_name = "client_init_resent";
        break;
    case picoquic_state_server_init:
        state_name = "server_init";
        break;
    case picoquic_state_server_handshake:
        state_name = "server_handshake";
        break;
    case picoquic_state_client_handshake_start:
        state_name = "client_handshake_start";
        break;
    case picoquic_state_client_handshake_progress:
        state_name = "client_handshake_progress";
        break;
    case picoquic_state_client_almost_ready:
        state_name = "client_almost_ready";
        break;
    case picoquic_state_handshake_failure:
        state_name = "handshake_failure";
        break;
    case picoquic_state_server_almost_ready:
        state_name = "server_almost_ready";
        break;
    case picoquic_state_client_ready:
        state_name = "client_ready";
        break;
    case picoquic_state_server_ready:
        state_name = "server_ready";
        break;
    case picoquic_state_disconnecting:
        state_name = "disconnecting";
        break;
    case picoquic_state_closing_received:
        state_name = "closing_received";
        break;
    case picoquic_state_closing:
        state_name = "closing";
        break;
    case picoquic_state_draining:
        state_name = "draining";
        break;
    case picoquic_state_disconnected:
        state_name = "disconnected";
        break;
    default:
        break;
    }
    return state_name;
}

char const* picoquic_log_ptype_name(picoquic_packet_type_enum ptype)
{
    char const* ptype_name = "unknown";

    switch (ptype) {
    case picoquic_packet_error:
        ptype_name = "error";
        break;
    case picoquic_packet_version_negotiation:
        ptype_name = "version negotiation";
        break;
    case picoquic_packet_initial:
        ptype_name = "initial";
        break;
    case picoquic_packet_retry:
        ptype_name = "retry";
        break;
    case picoquic_packet_handshake:
        ptype_name = "handshake";
        break;
    case picoquic_packet_0rtt_protected:
        ptype_name = "0rtt protected";
        break;
    case picoquic_packet_1rtt_protected_phi0:
        ptype_name = "1rtt protected phi0";
        break;
    case picoquic_packet_1rtt_protected_phi1:
        ptype_name = "1rtt protected phi1";
        break;
    default:
        break;
    }

    return ptype_name;
}

char const* picoquic_log_frame_names(uint64_t frame_type)
{
    char const * frame_name = "unknown";

    switch ((picoquic_frame_type_enum_t)frame_type) {
    case picoquic_frame_type_padding:
        frame_name = "padding";
        break;
    case picoquic_frame_type_reset_stream:
        frame_name = "reset_stream";
        break;
    case picoquic_frame_type_connection_close:
        frame_name = "connection_close";
        break;
    case picoquic_frame_type_application_close:
        frame_name = "application_close";
        break;
    case picoquic_frame_type_max_data:
        frame_name = "max_data";
        break;
    case picoquic_frame_type_max_stream_data:
        frame_name = "max_stream_data";
        break;
    case picoquic_frame_type_max_streams_bidi:
        frame_name = "max_stream_id";
        break;
    case picoquic_frame_type_ping:
        frame_name = "ping";
        break;
    case picoquic_frame_type_data_blocked:
        frame_name = "blocked";
        break;
    case picoquic_frame_type_stream_data_blocked:
        frame_name = "stream_blocked";
        break;
    case picoquic_frame_type_bidi_streams_blocked:
        frame_name = "stream_id_blocked";
        break;
    case picoquic_frame_type_new_connection_id:
        frame_name = "new_connection_id";
        break;
    case picoquic_frame_type_stop_sending:
        frame_name = "stop_sending";
        break;
    case picoquic_frame_type_ack:
        frame_name = "ack";
        break;
    case picoquic_frame_type_path_challenge:
        frame_name = "path_challenge";
        break;
    case picoquic_frame_type_path_response:
        frame_name = "path_response";
        break;
    case picoquic_frame_type_crypto_hs:
        frame_name = "crypto_hs";
        break;
    case picoquic_frame_type_new_token:
        frame_name = "new_token";
        break;
    case picoquic_frame_type_ack_ecn:
        frame_name = "ack_ecn";
        break;
    case picoquic_frame_type_plugin_validate:
        frame_name = "plugin_validate";
        break;
    default:
        if (PICOQUIC_IN_RANGE(frame_type, picoquic_frame_type_stream_range_min, picoquic_frame_type_stream_range_max)) {
            frame_name = "stream";
        }
        break;
    }

    return frame_name;
}

void picoquic_log_connection_id(FILE* F, picoquic_connection_id_t * cid)
{
    fprintf(F, "<");
    for (uint8_t i = 0; i < cid->id_len; i++) {
        fprintf(F, "%02x", cid->id[i]);
    }
    fprintf(F, ">");
}

void picoquic_log_packet_header(FILE* F, uint64_t log_cnxid64, picoquic_packet_header* ph, int receiving)
{
    if (log_cnxid64 != 0) {
        fprintf(F, "%" PRIx64 ": ", log_cnxid64);
    }

    fprintf(F, "%s packet type: %d (%s), ", (receiving != 0)?"Receiving":"Sending",
        ph->ptype, picoquic_log_ptype_name(ph->ptype));

    fprintf(F, "S%d,", ph->spin);

    switch (ph->ptype) {
    case picoquic_packet_1rtt_protected_phi0:
    case picoquic_packet_1rtt_protected_phi1:
        /* Short packets. Log dest CID and Seq number. */
        if (log_cnxid64 != 0) {
            fprintf(F, "\n%" PRIx64 ":     ", log_cnxid64);
        }
        picoquic_log_connection_id(F, &ph->dest_cnx_id);
        fprintf(F, ", Seq: %x (%" PRIx64 ")\n", ph->pn, ph->pn64);
        break;
    case picoquic_packet_version_negotiation:
        /* V nego. log both CID */
        if (log_cnxid64 != 0) {
            fprintf(F, "\n%" PRIx64 ":     ", log_cnxid64);
        }
        picoquic_log_connection_id(F, &ph->dest_cnx_id);
        fprintf(F, ", ");
        picoquic_log_connection_id(F, &ph->srce_cnx_id);
        fprintf(F, "\n");
        break;
    default:
        /* Long packets. Log Vnum, both CID, Seq num, Payload length */
        fprintf(F, " Version %x,", ph->vn);
        if (log_cnxid64 != 0) {
            fprintf(F, "\n%" PRIx64 ":     ", log_cnxid64);
        }
        picoquic_log_connection_id(F, &ph->dest_cnx_id);
        fprintf(F, ", ");
        picoquic_log_connection_id(F, &ph->srce_cnx_id);
        fprintf(F, ", Seq: %x, pl: %d\n", ph->pn, ph->payload_length);
        break;
    }
}

void picoquic_log_negotiation_packet(FILE* F, uint64_t log_cnxid64,
    uint8_t* bytes, size_t length, picoquic_packet_header* ph)
{
    size_t byte_index = ph->offset;
    uint32_t vn = 0;

    if (log_cnxid64 != 0) {
        fprintf(F, "%" PRIx64 ": ", log_cnxid64);
    }

    fprintf(F, "    versions: ");

    while (byte_index + 4 <= length) {
        vn = PICOPARSE_32(bytes + byte_index);
        byte_index += 4;
        fprintf(F, "%x, ", vn);
    }
    fprintf(F, "\n");
}

void picoquic_log_retry_packet(FILE* F, uint64_t log_cnxid64,
    uint8_t* bytes, size_t length, picoquic_packet_header* ph)
{
    size_t byte_index = ph->offset;
    int token_length = 0;
    uint8_t odcil;
    uint8_t unused_cil;
    int payload_length = (int)(length - ph->offset);

    picoquic_parse_packet_header_cnxid_lengths(bytes[byte_index++], &unused_cil, &odcil);

    if ((int)odcil > payload_length) {
        fprintf(F, "%" PRIx64 ": packet too short, ODCIL: %x (%d), only %d bytes available.\n",
            log_cnxid64, bytes[byte_index - 1], odcil, payload_length);
    } else {
        /* Dump the old connection ID */
        fprintf(F, "%" PRIx64 ":     ODCIL: <", log_cnxid64);
        for (uint8_t i = 0; i < odcil; i++) {
            fprintf(F, "%02x", bytes[byte_index++]);
        }

        token_length = ((int)ph->offset) + payload_length - ((int)byte_index);
        fprintf(F, ">, Token length: %d\n", token_length);
        /* Print the token or an error */
        if (token_length > 0) {
            int printed_length = (token_length > 16) ? 16 : token_length;
            fprintf(F, "%" PRIx64 ":     Token: ", log_cnxid64);
            for (uint8_t i = 0; i < printed_length; i++) {
                fprintf(F, "%02x", bytes[byte_index++]);
            }
            if (printed_length < token_length) {
                fprintf(F, "...");
            }
            fprintf(F, "\n");
        }
    }
    fprintf(F, "\n");
}

size_t picoquic_log_stream_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index;
    uint64_t stream_id;
    size_t data_length;
    uint64_t offset;
    int fin;
    int ret = 0;

    int suspended = debug_printf_reset(1);
    ret = picoquic_parse_stream_header(bytes, bytes_max,
        &stream_id, &offset, &data_length, &fin, &byte_index);
    (void)debug_printf_reset(suspended);

    if (ret != 0)
        return bytes_max;

    fprintf(F, "    Stream %" PRIu64 ", offset %" PRIu64 ", length %d, fin = %d", stream_id,
        offset, (int)data_length, fin);

    fprintf(F, ": ");
    for (size_t i = 0; i < 8 && i < data_length; i++) {
        fprintf(F, "%02x", bytes[byte_index + i]);
    }
    fprintf(F, "%s\n", (data_length > 8) ? "..." : "");

    return byte_index + data_length;
}

size_t picoquic_log_ack_frame(FILE* F, uint64_t cnx_id64, uint8_t* bytes, size_t bytes_max, int is_ecn)
{
    size_t byte_index;
    uint64_t num_block;
    uint64_t largest;
    uint64_t ack_delay;
    uint64_t ecnx3[3];

    int suspended = debug_printf_reset(1);

    int ret = picoquic_parse_ack_header(bytes, bytes_max, &num_block, (is_ecn)? ecnx3:NULL,
        &largest, &ack_delay, &byte_index, 0);

    (void)debug_printf_reset(suspended);

    if (ret != 0)
        return bytes_max;

    /* Now that the size is good, print it */
    if (is_ecn) {
        fprintf(F, "    ACK_ECN (nb=%u, ect0=%" PRIu64 ", ect1=%" PRIu64 ", ce=%" PRIu64 ")", (int)num_block,
            ecnx3[0], ecnx3[1], ecnx3[2]);
    }
    else {
        fprintf(F, "    ACK (nb=%u)", (int)num_block);
    }

    /* decoding the acks */

    while (ret == 0) {
        uint64_t range;
        uint64_t block_to_block;

        if (byte_index >= bytes_max) {
            fprintf(F, "    Malformed ACK RANGE, %d blocks remain.\n", (int)num_block);
            break;
        }

        size_t l_range = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &range);
        if (l_range == 0) {
            byte_index = bytes_max;
            fprintf(F, "    Malformed ACK RANGE, requires %d bytes out of %d", (int)picoquic_varint_skip(bytes),
                (int)(bytes_max - byte_index));
            break;
        } else {
            byte_index += l_range;
        }

        range++;

        if (largest + 1 < range) {
            fprintf(F, "ack range error: largest=%" PRIx64 ", range=%" PRIx64, largest, range);
            byte_index = bytes_max;
            break;
        }

        if (range <= 1)
            fprintf(F, ", %" PRIx64, largest);
        else
            fprintf(F, ", %" PRIx64 "-%" PRIx64, largest - range + 1, largest);

        if (num_block-- == 0)
            break;

        /* Skip the gap */

        if (byte_index >= bytes_max) {
            fprintf(F, "\n");
            if (cnx_id64 != 0) {
                fprintf(F, "%" PRIx64 ": ", cnx_id64);
            }
            fprintf(F, "    Malformed ACK GAP, %d blocks remain.", (int)num_block);
            byte_index = bytes_max;
            break;
        } else {
            size_t l_gap = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &block_to_block);
            if (l_gap == 0) {
                byte_index = bytes_max;
                fprintf(F, "\n");
                if (cnx_id64 != 0) {
                    fprintf(F, "%" PRIx64 ": ", cnx_id64);
                }
                fprintf(F, "    Malformed ACK GAP, requires %d bytes out of %d", (int)picoquic_varint_skip(bytes),
                    (int)(bytes_max - byte_index));
                break;
            } else {
                byte_index += l_gap;
                block_to_block += 1;
                block_to_block += range;
            }
        }

        if (largest < block_to_block) {
            fprintf(F, "\n");
            if (cnx_id64 != 0) {
                fprintf(F, "%" PRIx64 ": ", cnx_id64);
            }
            fprintf(F, "    ack gap error: largest=%" PRIx64 ", range=%" PRIx64 ", gap=%" PRIu64,
                largest, range, block_to_block - range);
            byte_index = bytes_max;
            break;
        }

        largest -= block_to_block;
    }

    fprintf(F, "\n");

    return byte_index;
}

size_t picoquic_log_reset_stream_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    uint64_t stream_id = 0;
    uint64_t error_code = 0;
    uint64_t offset = 0;

    size_t l1 = 0, l2 = 0, l3 = 0;
    if (bytes_max > 2) {
        l1 = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &stream_id);
        byte_index += l1;
        l2 = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &error_code);
        byte_index += l2;
        l3 = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &offset);
        byte_index += l3;
    }

    if (l1 == 0 || l2 == 0 || l3 == 0) {
        fprintf(F, "    Malformed RESET STREAM, requires %d bytes out of %d\n", (int)(byte_index + ((l1 == 0) ? (picoquic_varint_skip(bytes + 1) + 3) : picoquic_varint_skip(bytes + byte_index))),
            (int)bytes_max);
        byte_index = bytes_max;
    } else {
        fprintf(F, "    RESET STREAM %" PRIu64 ", Error 0x%" PRIx64 ", Offset 0x%" PRIx64 ".\n",
            stream_id, error_code, offset);
    }

    return byte_index;
}

size_t picoquic_log_stop_sending_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    const size_t min_size = 1 + picoquic_varint_skip(bytes + 1) + 1;
    uint64_t stream_id;
    uint64_t error_code;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed STOP SENDING, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    /* Now that the size is good, parse and print it */
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &stream_id);
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &error_code);

    fprintf(F, "    STOP SENDING %d (0x%08x), Error 0x%" PRIx64 ".\n",
        (uint32_t)stream_id, (uint32_t)stream_id, error_code);

    return byte_index;
}

size_t picoquic_log_generic_close_frame(FILE* F, uint8_t* bytes, size_t bytes_max, uint8_t ftype)
{
    size_t byte_index = 1;
    uint64_t error_code = 0;
    uint64_t string_length = 0;
    uint64_t offending_frame_type = 0;
    size_t lf = 0;
    size_t l1 = 0;

    if (bytes_max >= 3) {
        byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &error_code);
        if (ftype == picoquic_frame_type_connection_close) {
            lf = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &offending_frame_type);
            if (lf == 0) {
                byte_index = bytes_max;
            }
            else {
                byte_index += lf;
            }
        }
        if (ftype != picoquic_frame_type_connection_close || lf != 0) {
            l1 = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &string_length);
        }
    }

    if (l1 == 0) {
        fprintf(F, "    Malformed %s, requires %d bytes out of %d\n",
            picoquic_log_frame_names(ftype),
            (int)(byte_index + picoquic_varint_skip(bytes + 3)), (int)bytes_max);
        byte_index = bytes_max;
    }
    else {
        byte_index += l1;

        fprintf(F, "    %s, Error 0x%" PRIx64 ", ", picoquic_log_frame_names(ftype), error_code);
        if (ftype == picoquic_frame_type_connection_close &&
            offending_frame_type != 0) {
            fprintf(F, "Offending frame %" PRIx64 "\n",
                offending_frame_type);
        }
        fprintf(F, "Reason length %" PRIu64 "\n", string_length);
        if (byte_index + string_length > bytes_max) {
            fprintf(F, "    Malformed %s, requires %" PRIu64 " bytes out of %" PRIu64 "\n",
                picoquic_log_frame_names(ftype),
                (byte_index + string_length), bytes_max);
            byte_index = bytes_max;
        } else {
            /* TODO: print the UTF8 string */
            byte_index += (size_t)string_length;
        }
    }

    return byte_index;
}

size_t picoquic_log_connection_close_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    return picoquic_log_generic_close_frame(F, bytes, bytes_max, picoquic_frame_type_connection_close);
}

size_t picoquic_log_application_close_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    return picoquic_log_generic_close_frame(F, bytes, bytes_max, picoquic_frame_type_application_close);
}

size_t picoquic_log_max_data_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    uint64_t max_data;

    size_t l1 = picoquic_varint_decode(bytes + 1, bytes_max - 1, &max_data);

    if (1 + l1 > bytes_max) {
        fprintf(F, "    Malformed MAX DATA, requires %d bytes out of %d\n", (int)(1 + l1), (int)bytes_max);
        return bytes_max;
    } else {
        byte_index = 1 + l1;
    }

    fprintf(F, "    MAX DATA: 0x%" PRIx64 ".\n", max_data);

    return byte_index;
}

size_t picoquic_log_max_stream_data_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    uint64_t stream_id;
    uint64_t max_data;

    size_t l1 = picoquic_varint_decode(bytes + 1, bytes_max - 1, &stream_id);
    size_t l2 = picoquic_varint_decode(bytes + 1 + l1, bytes_max - 1 - l1, &max_data);

    if (l1 == 0 || l2 == 0) {
        fprintf(F, "    Malformed MAX STREAM DATA, requires %d bytes out of %d\n",
            (int)(1 + l1 + l2), (int)bytes_max);
        return bytes_max;
    } else {
        byte_index = 1 + l1 + l2;
    }

    fprintf(F, "    MAX STREAM DATA, Stream: %" PRIu64 ", max data: 0x%" PRIx64 ".\n",
        stream_id, max_data);

    return byte_index;
}

size_t picoquic_log_max_stream_id_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    const size_t min_size = 1 + picoquic_varint_skip(bytes + 1);
    uint64_t max_stream_id;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed MAX STREAM ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    /* Now that the size is good, parse and print it */
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &max_stream_id);

    fprintf(F, "    MAX STREAM ID: %" PRIu64 ".\n", max_stream_id);

    return byte_index;
}

size_t picoquic_log_blocked_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    const size_t min_size = 1 + picoquic_varint_skip(bytes + 1);
    uint64_t blocked_offset = 0;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed BLOCKED, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    /* Now that the size is good, parse and print it */
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &blocked_offset);

    fprintf(F, "    BLOCKED: offset %" PRIu64 ".\n",
        blocked_offset);

    return byte_index;
}

size_t picoquic_log_stream_blocked_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    const size_t min_size = 1 + picoquic_varint_skip(bytes + 1);
    uint64_t blocked_stream_id;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed STREAM BLOCKED, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    /* Now that the size is good, parse and print it */
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &blocked_stream_id);
    byte_index += picoquic_varint_skip(&bytes[byte_index]);

    fprintf(F, "    STREAM BLOCKED: %" PRIu64 ".\n",
        blocked_stream_id);

    return byte_index;
}

size_t picoquic_log_new_connection_id_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    size_t min_size = 1 + 8 + 16;
    picoquic_connection_id_t new_cnx_id;
    size_t l_seq = 0;
    uint8_t l_cid = 0;

    l_seq = picoquic_varint_skip(&bytes[byte_index]);

    min_size += l_seq;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed NEW CONNECTION ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    byte_index += l_seq;

    if (byte_index < bytes_max) {
        l_cid = bytes[byte_index++];
    }

    if (byte_index + l_cid + 16 > bytes_max) {
        fprintf(F, "    Malformed NEW CONNECTION ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        byte_index = bytes_max;
    }
    else {
        byte_index += picoquic_parse_connection_id(bytes + byte_index, l_cid, &new_cnx_id);
        fprintf(F, "    NEW CONNECTION ID: 0x");
        for (int x = 0; x < new_cnx_id.id_len; x++) {
            fprintf(F, "%02x", new_cnx_id.id[x]);
        }
        fprintf(F, ", ");
        for (int x = 0; x < 16; x++) {
            fprintf(F, "%02x", bytes[byte_index++]);
        }
        fprintf(F, "\n");
    }

    return byte_index;
}

size_t picoquic_log_new_token_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    size_t min_size = 1;
    size_t l_toklen = 0;
    uint64_t toklen = 0;

    l_toklen = picoquic_varint_decode(&bytes[byte_index], bytes_max, &toklen);

    min_size += l_toklen + (size_t)toklen;

    if (l_toklen == 0 || min_size > bytes_max) {
        fprintf(F, "    Malformed NEW CONNECTION ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    } else {
        byte_index += l_toklen;
        fprintf(F, "    NEW TOKEN[%d]: 0x", (int)toklen);
        for (int x = 0; x < toklen && x < 16; x++) {
            fprintf(F, "%02x", bytes[byte_index + x]);
        }
        byte_index += (size_t)toklen;

        if (toklen > 16) {
            fprintf(F, "...");
        }
        fprintf(F, "\n");
    }

    return byte_index;
}

size_t picoquic_log_path_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    size_t challenge_length = 8;

    if (byte_index + challenge_length > bytes_max) {
        fprintf(F, "    Malformed %s frame, %d bytes needed, %d available\n",
            picoquic_log_frame_names(bytes[0]),
            (int)(challenge_length + 1), (int)bytes_max);
        byte_index = bytes_max;
    } else {
        fprintf(F, "    %s: ", picoquic_log_frame_names(bytes[0]));

        for (size_t i = 0; i < challenge_length && i < 16; i++) {
            fprintf(F, "%02x", bytes[byte_index + i]);
        }

        if (challenge_length > 16) {
            fprintf(F, " ...");
        }
        fprintf(F, "\n");

        byte_index += challenge_length;
    }

    return byte_index;
}

size_t picoquic_log_crypto_hs_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    uint64_t offset=0;
    uint64_t data_length = 0;
    size_t byte_index = 1;
    size_t l_off = 0;
    size_t l_len = 0;

    if (bytes_max > byte_index) {
        l_off = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &offset);
        byte_index += l_off;
    }

    if (bytes_max > byte_index) {
        l_len = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &data_length);
        byte_index += l_len;
    }

    if (l_off == 0 || l_len == 0 || byte_index + data_length > bytes_max) {
        fprintf(F, "    Malformed Crypto HS frame.\n");
        byte_index = bytes_max;
    } else {

        fprintf(F, "    Crypto HS frame, offset %" PRIu64 ", length %d", offset, (int)data_length);

        fprintf(F, ": ");
        for (size_t i = 0; i < 8 && i < data_length; i++) {
            fprintf(F, "%02x", bytes[byte_index + i]);
        }
        fprintf(F, "%s\n", (data_length > 8) ? "..." : "");

        byte_index += (size_t)data_length;
    }

    return byte_index;
}

size_t picoquic_log_plugin_validate_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 1;
    uint64_t pid_id;
    uint64_t pid_len;

    size_t l1 = picoquic_varint_decode(bytes + 1, bytes_max - 1, &pid_id);

    if (1 + l1 > bytes_max) {
        fprintf(F, "    Malformed PLUGIN VALIDATE, requires %d bytes out of %d\n", (int)(1 + l1), (int)bytes_max);
        return bytes_max;
    }

    byte_index = 1 + l1;

    size_t l2 = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &pid_len);
    if (byte_index + l1 > bytes_max) {
        fprintf(F, "    Malformed PLUGIN VALIDATE, requires %d bytes out of %d\n", (int)(byte_index + l1), (int)bytes_max);
        return bytes_max;
    }
    byte_index += l2;

    if (byte_index + pid_len > bytes_max) {
        fprintf(F, "    Malformed PLUGIN VALIDATE, requires %d bytes out of %d\n", (int)(byte_index + pid_len), (int)bytes_max);
        return bytes_max;
    }

    char pid[pid_len];
    memcpy(pid, bytes + byte_index, pid_len);
    byte_index += pid_len;

    fprintf(F, "    PLUGIN VALIDATE: ID %" PRIx64 " for %s.\n", pid_id, pid);

    return byte_index;
}

size_t picoquic_log_plugin_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    uint8_t fin = 0;
    uint64_t pid_id = 0;
    uint64_t offset = 0;
    uint64_t data_length = 0;
    size_t byte_index = 2;
    size_t l_pid = 0;
    size_t l_off = 0;
    size_t l_len = 0;

    if (bytes_max > byte_index) {
        fin = bytes[1];
        l_pid = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &pid_id);
        byte_index += l_pid;
    }

    if (bytes_max > byte_index) {
        l_off = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &offset);
        byte_index += l_off;
    }

    if (bytes_max > byte_index) {
        l_len = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &data_length);
        byte_index += l_len;
    }

    if (l_off == 0 || l_len == 0 || byte_index + data_length > bytes_max) {
        fprintf(F, "    Malformed Plugin frame.\n");
        byte_index = bytes_max;
    } else {

        fprintf(F, "    PLUGIN frame, PID_ID %" PRIu64 ", FIN %d, offset %" PRIu64 ", length %d", pid_id, fin, offset, (int)data_length);

        fprintf(F, ": ");
        for (size_t i = 0; i < 8 && i < data_length; i++) {
            fprintf(F, "%02x", bytes[byte_index + i]);
        }
        fprintf(F, "%s\n", (data_length > 8) ? "..." : "");

        byte_index += (size_t)data_length;
    }

    return byte_index;
}

size_t picoquic_log_add_address_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 2;

    uint8_t has_port = 0;
    uint8_t ip_vers;
    uint8_t addr_id;

    uint8_t flags_and_ip_ver = bytes[byte_index++];
    has_port = (flags_and_ip_ver & 0x10);
    ip_vers = (flags_and_ip_ver & 0x0f);
    addr_id = bytes[byte_index++];

    char hostname[256];
    const char* x;

    struct sockaddr_in sa;
    struct sockaddr_in6 sa6;

    if (ip_vers == 4) {
        memcpy(&sa.sin_addr.s_addr, &bytes[byte_index], 4);
        byte_index += 4;
        if (has_port) {
            memcpy(&sa.sin_port, &bytes[byte_index], 2);
            byte_index += 2;
        }
        x = inet_ntop(AF_INET, &sa.sin_addr, hostname, sizeof(hostname));

    } else if (ip_vers == 6) {
        memcpy(&sa6.sin6_addr, &bytes[byte_index], 16);
        byte_index += 16;
        if (has_port) {
            memcpy(&sa6.sin6_port, &bytes[byte_index], 2);
            byte_index += 2;
        }
        x = inet_ntop(AF_INET6, &sa6.sin6_addr, hostname, sizeof(hostname));

    } else {
        fprintf(F, "    Malformed ADD ADDRESS, unknown IP version %d\n", (int)ip_vers);
        return bytes_max;
    }

    fprintf(F, "    ADD ADDRESS with ID 0x");
    fprintf(F, "%02x", addr_id);
    fprintf(F, " Address: ");
    fprintf(F, "%s", x);
    if (has_port) {
        fprintf(F, " Port: ");
        fprintf(F, "%d", (ip_vers == 4) ? sa.sin_port : sa6.sin6_port);
    }
    fprintf(F, "\n");

    return byte_index;
}

size_t picoquic_log_mp_new_connection_id_frame(FILE* F, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index = 2;
    size_t min_size = 2 + 8 + 16;
    picoquic_connection_id_t new_cnx_id;
    size_t l_seq = 0;
    uint8_t l_cid = 0;
    uint64_t path_id = 0;
    size_t l_path_id = 0;

    l_path_id = picoquic_varint_decode(&bytes[byte_index], bytes_max, &path_id);

    min_size += l_path_id;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed MP NEW CONNECTION ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    byte_index += l_path_id;

    l_seq = picoquic_varint_skip(&bytes[byte_index]);

    min_size += l_seq;

    if (min_size > bytes_max) {
        fprintf(F, "    Malformed MP NEW CONNECTION ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        return bytes_max;
    }

    byte_index += l_seq;

    if (byte_index < bytes_max) {
        l_cid = bytes[byte_index++];
    }

    if (byte_index + l_cid + 16 > bytes_max) {
        fprintf(F, "    Malformed MP NEW CONNECTION ID, requires %d bytes out of %d\n", (int)min_size, (int)bytes_max);
        byte_index = bytes_max;
    }
    else {
        byte_index += picoquic_parse_connection_id(bytes + byte_index, l_cid, &new_cnx_id);
        fprintf(F, "    MP NEW CONNECTION ID for Uniflow 0x");
        fprintf(F, "%02lx", path_id);
        fprintf(F, " CID: 0x");
        for (int x = 0; x < new_cnx_id.id_len; x++) {
            fprintf(F, "%02x", new_cnx_id.id[x]);
        }
        fprintf(F, ", ");
        for (int x = 0; x < 16; x++) {
            fprintf(F, "%02x", bytes[byte_index++]);
        }
        fprintf(F, "\n");
    }

    return byte_index;
}

static int parse_mp_ack_header(uint8_t const* bytes, size_t bytes_max,
    uint64_t* num_block, uint64_t* nb_ecnx3, uint64_t *path_id,
    uint64_t* largest, uint64_t* ack_delay, size_t* consumed,
    uint8_t ack_delay_exponent)
{
    int ret = 0;
    size_t byte_index = 2;
    size_t l_largest = 0;
    size_t l_delay = 0;
    size_t l_blocks = 0;
    size_t l_path_id = 0;

    if (bytes_max > byte_index) {
        l_path_id = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, path_id);
        byte_index += l_path_id;
    }

    if (bytes_max > byte_index) {
        l_largest = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, largest);
        byte_index += l_largest;
    }

    if (bytes_max > byte_index) {
        l_delay = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, ack_delay);
        *ack_delay <<= ack_delay_exponent;
        byte_index += l_delay;
    }

    if (nb_ecnx3 != NULL) {
        for (int ecnx = 0; ecnx < 3; ecnx++) {
            size_t l_ecnx = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &nb_ecnx3[ecnx]);

            if (l_ecnx == 0) {
                byte_index = bytes_max;
            }
            else {
                byte_index += l_ecnx;
            }
        }
    }

    if (bytes_max > byte_index) {
        l_blocks = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, num_block);
        byte_index += l_blocks;
    }

    if (l_path_id == 0 || l_largest == 0 || l_delay == 0 || l_blocks == 0 || bytes_max < byte_index) {
        // DBG_PRINTF("ack frame fixed header too large: first_byte=0x%02x, bytes_max=%" PRIst,
        //     bytes[0], bytes_max);
        byte_index = bytes_max;
        ret = -1;
    }

    *consumed = byte_index;
    return ret;
}

size_t picoquic_log_mp_ack_frame(FILE* F, uint64_t cnx_id64, uint8_t* bytes, size_t bytes_max)
{
    size_t byte_index;
    uint64_t path_id;
    uint64_t num_block;
    uint64_t largest;
    uint64_t ack_delay;

    int suspended = debug_printf_reset(1);

    int ret = parse_mp_ack_header(bytes, bytes_max, &num_block, NULL, &path_id,
        &largest, &ack_delay, &byte_index, 0);

    (void)debug_printf_reset(suspended);

    if (ret != 0)
        return bytes_max;

    /* Now that the size is good, print it */
    fprintf(F, "    MP ACK for uniflow 0x%02lx (nb=%u)", path_id, (int)num_block);

    /* decoding the acks */

    while (ret == 0) {
        uint64_t range;
        uint64_t block_to_block;

        if (byte_index >= bytes_max) {
            fprintf(F, "    Malformed ACK RANGE, %d blocks remain.\n", (int)num_block);
            break;
        }

        size_t l_range = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &range);
        if (l_range == 0) {
            byte_index = bytes_max;
            fprintf(F, "    Malformed ACK RANGE, requires %d bytes out of %d", (int)picoquic_varint_skip(bytes),
                (int)(bytes_max - byte_index));
            break;
        } else {
            byte_index += l_range;
        }

        range++;

        if (largest + 1 < range) {
            fprintf(F, "ack range error: largest=%" PRIx64 ", range=%" PRIx64, largest, range);
            byte_index = bytes_max;
            break;
        }

        if (range <= 1)
            fprintf(F, ", %" PRIx64, largest);
        else
            fprintf(F, ", %" PRIx64 "-%" PRIx64, largest - range + 1, largest);

        if (num_block-- == 0)
            break;

        /* Skip the gap */

        if (byte_index >= bytes_max) {
            fprintf(F, "\n");
            if (cnx_id64 != 0) {
                fprintf(F, "%" PRIx64 ": ", cnx_id64);
            }
            fprintf(F, "    Malformed ACK GAP, %d blocks remain.", (int)num_block);
            byte_index = bytes_max;
            break;
        } else {
            size_t l_gap = picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &block_to_block);
            if (l_gap == 0) {
                byte_index = bytes_max;
                fprintf(F, "\n");
                if (cnx_id64 != 0) {
                    fprintf(F, "%" PRIx64 ": ", cnx_id64);
                }
                fprintf(F, "    Malformed ACK GAP, requires %d bytes out of %d", (int)picoquic_varint_skip(bytes),
                    (int)(bytes_max - byte_index));
                break;
            } else {
                byte_index += l_gap;
                block_to_block += 1;
                block_to_block += range;
            }
        }

        if (largest < block_to_block) {
            fprintf(F, "\n");
            if (cnx_id64 != 0) {
                fprintf(F, "%" PRIx64 ": ", cnx_id64);
            }
            fprintf(F, "    ack gap error: largest=%" PRIx64 ", range=%" PRIx64 ", gap=%" PRIu64,
                largest, range, block_to_block - range);
            byte_index = bytes_max;
            break;
        }

        largest -= block_to_block;
    }

    fprintf(F, "\n");

    return byte_index;
}

size_t picoquic_log_datagram_frame(FILE* F, uint64_t cnx_id64, uint8_t* bytes, size_t bytes_max) {
    size_t byte_index = 0;
    uint64_t len = 0;
    uint64_t datagram_id = 0;
    /* uint8_t *payload = NULL; */ // Unused

    uint8_t type_byte = *bytes;
    byte_index++;
    switch (type_byte) {
        case 0x2c:
            len = bytes_max - byte_index;
            fprintf(F, "    DATAGRAM\n");
            break;
        case 0x2d:
            byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &len);
            fprintf(F, "    DATAGRAM (len=%" PRIu64 ")\n", len);
            break;
        case 0x2e:
            byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &datagram_id);
            len = bytes_max - byte_index;
            fprintf(F, "    DATAGRAM (id=%" PRIu64 ")\n", datagram_id);
        case 0x2f:
            byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &datagram_id);
            byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &len);
            fprintf(F, "    DATAGRAM (id=%" PRIu64 ", len=%" PRIu64 ")\n", datagram_id, len);
            break;
    }
    byte_index += len;

    return byte_index;
}

size_t picoquic_log_fec_window_rwin_frame(FILE* F, uint64_t cnx_id64, uint8_t* bytes, size_t bytes_max) {
    size_t byte_index = 0;
    byte_index++;
    uint64_t smallest_id = 0;
    uint64_t window_size = 0;
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &smallest_id);
    byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &window_size);

    fprintf(F, " \tWINDOW RWIN FRAME: [%lu, %lu[ (size = %lu)\n", smallest_id, smallest_id + window_size, window_size);
    return byte_index;
}

size_t picoquic_log_sfpid_frame(FILE* F, uint64_t cnx_id64, uint8_t* bytes, size_t bytes_max) {
    uint64_t val = 0;
    picoquic_varint_decode(bytes + 1, bytes_max - 1, &val);
    fprintf(F, " \tSFPID FRAME: ID %lu\n", val);
    return 5;
}

void picoquic_log_frames(FILE* F, uint64_t cnx_id64, uint8_t* bytes, size_t length)
{
    size_t byte_index = 0;
    uint64_t frame_id;

    while (byte_index < length) {
        picoquic_varint_decode(bytes + byte_index, length - byte_index, &frame_id);

        if (cnx_id64 != 0) {
            fprintf(F, "%" PRIx64 ": ", cnx_id64);
        }

        if (PICOQUIC_IN_RANGE(frame_id, picoquic_frame_type_stream_range_min, picoquic_frame_type_stream_range_max)) {
            byte_index += picoquic_log_stream_frame(F, bytes + byte_index, length - byte_index);
            continue;
        }

        switch (frame_id) {
        case picoquic_frame_type_ack:
            byte_index += picoquic_log_ack_frame(F, cnx_id64, bytes + byte_index, length - byte_index, 0);
            break;
        case picoquic_frame_type_ack_ecn:
            byte_index += picoquic_log_ack_frame(F, cnx_id64, bytes + byte_index, length - byte_index, 1);
            break;
        case picoquic_frame_type_padding:
        case picoquic_frame_type_ping: {
            int nb = 0;

            while (byte_index < length && bytes[byte_index] == frame_id) {
                byte_index++;
                nb++;
            }

            fprintf(F, "    %s, %d bytes\n", picoquic_log_frame_names(frame_id), nb);
            break;
        }
        case picoquic_frame_type_reset_stream: /* RST_STREAM */
            byte_index += picoquic_log_reset_stream_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_connection_close: /* CONNECTION_CLOSE */
            byte_index += picoquic_log_connection_close_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_application_close:
            byte_index += picoquic_log_application_close_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_max_data: /* MAX_DATA */
            byte_index += picoquic_log_max_data_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_max_stream_data: /* MAX_STREAM_DATA */
            byte_index += picoquic_log_max_stream_data_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_max_streams_bidi: /* MAX_STREAM_ID */
            byte_index += picoquic_log_max_stream_id_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_data_blocked: /* BLOCKED */
            /* No payload */
            byte_index += picoquic_log_blocked_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_stream_data_blocked: /* STREAM_BLOCKED */
            byte_index += picoquic_log_stream_blocked_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_bidi_streams_blocked: /* STREAM_ID_BLOCKED */
            /* No payload */
            fprintf(F, "    %s frame\n", picoquic_log_frame_names(frame_id));
            byte_index++;
            byte_index += picoquic_varint_skip(&bytes[byte_index]);
            break;
        case picoquic_frame_type_new_connection_id: /* NEW_CONNECTION_ID */
            byte_index += picoquic_log_new_connection_id_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_stop_sending: /* STOP_SENDING */
            byte_index += picoquic_log_stop_sending_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_path_challenge:
            byte_index += picoquic_log_path_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_path_response:
            byte_index += picoquic_log_path_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_crypto_hs:
            byte_index += picoquic_log_crypto_hs_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_new_token:
            byte_index += picoquic_log_new_token_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_handshake_done:
            fprintf(F, "    HANDSHAKE_DONE\n");
            byte_index += 1;
            break;
        case 0x2c: /* DATAGRAM */
        case 0x2d:
        case 0x2e:
        case 0x2f:
            byte_index += picoquic_log_datagram_frame(F, cnx_id64, bytes + byte_index, length - byte_index);
            break;
        case picoquic_frame_type_plugin_validate:
            byte_index += picoquic_log_plugin_validate_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case picoquic_frame_type_plugin:
            byte_index += picoquic_log_plugin_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case 0x28: /* FEC WINDOW RWIN */
            byte_index += picoquic_log_fec_window_rwin_frame(F, cnx_id64, bytes + byte_index,
                                                             length - byte_index);
            break;
        case 0x29: /* FEC SFPID */
            byte_index += picoquic_log_sfpid_frame(F, cnx_id64, bytes + byte_index,
                                                   length - byte_index);
            break;
        case 0x40: /* MP_NEW_CONNECTION_ID */
            byte_index += picoquic_log_mp_new_connection_id_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        case 0x42: /* MP ACK */
            byte_index += picoquic_log_mp_ack_frame(F, cnx_id64, bytes + byte_index,
                length - byte_index);
            break;
        case 0x44: /* ADD_ADDRESS */
            byte_index += picoquic_log_add_address_frame(F, bytes + byte_index,
                length - byte_index);
            break;
        default: {
            /* Not implemented yet! */
            uint64_t frame_id64;
            if (picoquic_varint_decode(bytes + byte_index, length - byte_index, &frame_id64) > 0) {
                fprintf(F, "    Unknown frame, type: %" PRIu64 "\n", frame_id64);
            } else {
                fprintf(F, "    Truncated frame type\n");
            }
            byte_index = length;
            break;
        }
        }
    }
}

void picoquic_log_decrypted_segment(void* F_log, int log_cnxid, picoquic_cnx_t* cnx,
    int receiving, picoquic_packet_header * ph, uint8_t* bytes, size_t length, int ret)
{
    uint64_t log_cnxid64 = 0;
    FILE * F = (FILE *)F_log;

    if (F == NULL) {
        return;
    }

    if (log_cnxid != 0) {
        if (cnx == NULL) {
            ph->pn64 = ph->pn;
            if (ret == 0) {
                if (ph->ptype == picoquic_packet_version_negotiation) {
                    log_cnxid64 = picoquic_val64_connection_id(ph->srce_cnx_id);
                }
                else {
                    log_cnxid64 = picoquic_val64_connection_id(ph->dest_cnx_id);
                }
            }
        }
        else {
            log_cnxid64 = picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx));
        }
    }
    /* Header */
    picoquic_log_packet_header(F, log_cnxid64, ph, receiving);

    if (ret != 0) {
        /* packet does parse or decrypt */
        if (log_cnxid64 != 0) {
            fprintf(F, "%" PRIx64 ": ", log_cnxid64);
        }
        if (ret == PICOQUIC_ERROR_STATELESS_RESET) {
            fprintf(F, "   Stateless reset.\n");
        }
        else {
            fprintf(F, "   Header or encryption error: %x.\n", ret);
        }
    }
    else if (ph->ptype == picoquic_packet_version_negotiation) {
        /* log version negotiation */
        picoquic_log_negotiation_packet(F, log_cnxid64, bytes, length, ph);
    }
    else if (ph->ptype == picoquic_packet_retry) {
        /* log version negotiation */
        picoquic_log_retry_packet(F, log_cnxid64, bytes, length, ph);
    }
    else if (ph->ptype != picoquic_packet_error) {
        /* log frames inside packet */
        if (log_cnxid64 != 0) {
            fprintf(F, "%" PRIx64 ": ", log_cnxid64);
        }
        fprintf(F, "    %s %d bytes\n", (receiving)?"Decrypted": "Prepared",
            (int)ph->payload_length);
        picoquic_log_frames(F, log_cnxid64, bytes + ph->offset,
            ph->payload_length);
    }
    fprintf(F, "\n");
}

void picoquic_log_outgoing_segment(void* F_log, int log_cnxid, picoquic_cnx_t* cnx,
    uint8_t * bytes,
    uint64_t sequence_number,
    uint32_t length,
    uint8_t* send_buffer, uint32_t send_length)
{
    picoquic_cnx_t* pcnx = cnx;
    picoquic_packet_header ph;
    uint32_t checksum_length = (cnx != NULL)? picoquic_get_checksum_length(cnx, 0):16;
    struct sockaddr_in default_addr;
    int ret;

    if (F_log == NULL) {
        return;
    }

    memset(&default_addr, 0, sizeof(struct sockaddr_in));
    default_addr.sin_family = AF_INET;

    ret = picoquic_parse_packet_header((cnx == NULL) ? NULL : cnx->quic, send_buffer, send_length,
        ((cnx==NULL || cnx->path[0] == NULL)?(struct sockaddr *)&default_addr:
        (struct sockaddr *)&cnx->path[0]->local_addr), &ph, &pcnx, 0);

    ph.pn64 = sequence_number;
    ph.pn = (uint32_t)ph.pn64;
    ph.offset = ph.pn_offset + 4; /* todo: should provide the actual length */
    ph.payload_length -= 4;
    if (ph.payload_length > checksum_length) {
        ph.payload_length -= (uint16_t)checksum_length;
    }
    else {
        ph.payload_length = 0;
    }

    /* log the segment. */
    picoquic_log_decrypted_segment(F_log, log_cnxid, cnx, 0,
        &ph, bytes, length, ret);
}

void picoquic_log_processing(FILE* F, picoquic_cnx_t* cnx, size_t length, int ret)
{
    if(!F) {
        return;
    }
    fprintf(F, "Processed %d bytes, state = %d (%s), return %d\n\n",
        (int)length, cnx->cnx_state,
        picoquic_log_state_name(cnx->cnx_state),
        ret);
}

void picoquic_log_transport_extension_content(FILE* F, int log_cnxid, uint64_t cnx_id_64,
    uint8_t * bytes, size_t bytes_max, int client_mode,
    uint32_t initial_version, uint32_t final_version)
{
    int ret = 0;
    size_t byte_index = 0;

    if (bytes_max < 256)
    {
        if (ret == 0)
        {
            if (byte_index + 2 > bytes_max) {
                if (log_cnxid != 0) {
                    fprintf(F, "%" PRIx64 ": ", cnx_id_64);
                }
                fprintf(F, "    Malformed extension list, only %d byte avaliable.\n", (int)(bytes_max - byte_index));
                ret = -1;
            }
            else {
                if (log_cnxid != 0) {
                    fprintf(F, "%" PRIx64 ": ", cnx_id_64);
                }
                fprintf(F, "    Extension list (%d bytes):\n", (uint32_t) bytes_max);
                while (ret == 0 && byte_index < bytes_max) {
                    if (byte_index + 4 > bytes_max) {
                        if (log_cnxid != 0) {
                            fprintf(F, "%" PRIx64 ": ", cnx_id_64);
                        }
                        fprintf(F, "        Malformed extension -- only %d bytes avaliable for type and length.\n",
                            (int)(bytes_max - byte_index));
                        ret = -1;
                    }
                    else {
                        uint64_t extension_type;
                        byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &extension_type);
                        uint64_t extension_length;
                        byte_index += picoquic_varint_decode(bytes + byte_index, bytes_max - byte_index, &extension_length);

                        if (log_cnxid != 0) {
                            fprintf(F, "%" PRIx64 ": ", cnx_id_64);
                        }
                        fprintf(F, "        Extension type: %lu, length %lu (0x%04lx / 0x%04lx), ",
                            extension_type, extension_length, extension_type, extension_length);

                        if (byte_index + extension_length > bytes_max) {
                            if (log_cnxid != 0) {
                                fprintf(F, "\n%" PRIx64 ": ", cnx_id_64);
                            }
                            fprintf(F, "Malformed extension, only %d bytes available.\n", (int)(bytes_max - byte_index));
                            ret = -1;
                        }
                        else {
                            char *format_str = "%02x";
                            if (extension_type == picoquic_tp_supported_plugins || extension_type == picoquic_tp_plugins_to_inject) {
                                format_str = "%c";
                            }
                            for (uint16_t i = 0; i < extension_length; i++) {
                                fprintf(F, format_str, bytes[byte_index++]);
                            }
                            fprintf(F, "\n");
                        }
                    }
                }
            }
        }

        if (ret == 0 && byte_index < bytes_max) {
            if (log_cnxid != 0) {
                fprintf(F, "%" PRIx64 ": ", cnx_id_64);
            }
            fprintf(F, "    Remaining bytes (%d)\n", (uint32_t)(bytes_max - byte_index));
        }
    }
    else {
        if (log_cnxid != 0) {
            fprintf(F, "%" PRIx64 ": ", cnx_id_64);
        }
        fprintf(F, "Received transport parameter TLS extension (%d bytes):\n", (uint32_t)bytes_max);
        if (log_cnxid != 0) {
            fprintf(F, "%" PRIx64 ": ", cnx_id_64);
        }
        fprintf(F, "    First bytes (%d):\n", (uint32_t)(bytes_max - byte_index));
    }

    if (ret == 0)
    {
        while (byte_index < bytes_max && byte_index < 128) {
            if (log_cnxid != 0) {
                fprintf(F, "%" PRIx64 ": ", cnx_id_64);
            }
            fprintf(F, "        ");
            for (int i = 0; i < 32 && byte_index < bytes_max && byte_index < 128; i++) {
                fprintf(F, "%02x", bytes[byte_index++]);
            }
            fprintf(F, "\n");
        }
    }
}

void picoquic_log_transport_extension(FILE* F, picoquic_cnx_t* cnx, int log_cnxid)
{
    if (!F)
        return;

    uint8_t* bytes = NULL;
    size_t bytes_max = 0;
    int ext_received_return = 0;
    int client_mode = 1;
    char const* sni = picoquic_tls_get_sni(cnx);
    char const* alpn = picoquic_tls_get_negotiated_alpn(cnx);

    if (log_cnxid != 0) {
        fprintf(F, "%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
    }
    if (sni == NULL) {
        fprintf(F, "SNI not received.\n");
    } else {
        fprintf(F, "Received SNI: %s\n", sni);
    }

    if (log_cnxid != 0) {
        fprintf(F, "%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
    }
    if (alpn == NULL) {
        fprintf(F, "ALPN not received.\n");
    } else {
        fprintf(F, "Received ALPN: %s\n", alpn);
    }
    picoquic_provide_received_transport_extensions(cnx,
        &bytes, &bytes_max, &ext_received_return, &client_mode);

    if (bytes_max == 0) {
        if (log_cnxid != 0) {
            fprintf(F, "%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        }
        fprintf(F, "Did not receive transport parameter TLS extension.\n");
    }
    else {
        if (log_cnxid != 0) {
            fprintf(F, "%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        }
        fprintf(F, "Received transport parameter TLS extension (%d bytes):\n", (uint32_t)bytes_max);

        picoquic_log_transport_extension_content(F, log_cnxid,
            picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)), bytes, bytes_max, client_mode,
            cnx->proposed_version, picoquic_supported_versions[cnx->version_index].version);
    }

    if (log_cnxid == 0) {
        fprintf(F, "\n");
    }
}

void picoquic_log_congestion_state(FILE* F, picoquic_cnx_t* cnx, uint64_t current_time)
{
    if (F != NULL) {
        picoquic_path_t * path_x = cnx->path[0];

        fprintf(F, "%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        picoquic_log_time(F, cnx, current_time, "T= ", ", ");
        fprintf(F, "cwin: %d,", (int)path_x->cwin);
        fprintf(F, "flight: %d,", (int)path_x->bytes_in_transit);
        fprintf(F, "nb_ret: %d,", (int)cnx->nb_retransmission_total);
        fprintf(F, "rtt_min: %d,", (int)path_x->rtt_min);
        fprintf(F, "rtt: %d,", (int)path_x->smoothed_rtt);
        fprintf(F, "rtt_var: %d,", (int)path_x->rtt_variant);
        fprintf(F, "max_ack_delay: %d,", (int)path_x->max_ack_delay);
        fprintf(F, "state: %d\n", (int)cnx->cnx_state);
    }
}

/*
    From TLS 1.3 spec:
   struct {
       uint32 ticket_lifetime;
       uint32 ticket_age_add;
       opaque ticket_nonce<0..255>;
       opaque ticket<1..2^16-1>;
       Extension extensions<0..2^16-2>;
   } NewSessionTicket;

   struct {
       ExtensionType extension_type;
       opaque extension_data<0..2^16-1>;
   } Extension;
*/
static void picoquic_log_tls_ticket(FILE* F, picoquic_connection_id_t cnx_id,
    uint8_t* ticket, uint16_t ticket_length)
{
    uint32_t lifetime = 0;
    uint32_t age_add = 0;
    uint8_t nonce_length = 0;
    uint16_t ticket_val_length = 0;
    uint16_t extension_length = 0;
    uint8_t* extension_ptr = NULL;
    uint16_t byte_index = 0;
    uint16_t min_length = 4 + 4 + 1 + 2 + 2;
    int ret = 0;

    if (ticket_length < min_length) {
        ret = -1;
    } else {
        lifetime = PICOPARSE_32(ticket);
        byte_index += 4;
        age_add = PICOPARSE_32(ticket + byte_index);
        byte_index += 4;
        nonce_length = ticket[byte_index++];
        min_length += nonce_length;
        if (ticket_length < min_length) {
            ret = -1;
        } else {
            byte_index += nonce_length;

            ticket_val_length = PICOPARSE_16(ticket + byte_index);
            byte_index += 2;
            min_length += ticket_val_length;
            if (ticket_length < min_length) {
                ret = -1;
            } else {
                byte_index += ticket_val_length;

                extension_length = PICOPARSE_16(ticket + byte_index);
                byte_index += 2;
                min_length += extension_length;
                if (ticket_length < min_length) {
                    ret = -1;
                } else {
                    extension_ptr = &ticket[byte_index];
                    if (min_length > ticket_length) {
                        ret = -2;
                    }
                }
            }
        }
    }

    if (ret == -1) {
        fprintf(F, "%" PRIu64 ": Malformed ticket, length = %d, at least %d required.\n",
            picoquic_val64_connection_id(cnx_id), ticket_length, min_length);
    }
    fprintf(F, "%" PRIu64 ": lifetime = %d, age_add = %x, %d nonce, %d ticket, %d extensions.\n",
        picoquic_val64_connection_id(cnx_id), lifetime, age_add, nonce_length, ticket_val_length, extension_length);

    if (extension_ptr != NULL) {
        uint16_t x_index = 0;

        fprintf(F, "%" PRIu64 ": ticket extensions: ", picoquic_val64_connection_id(cnx_id));

        while (x_index + 4 < extension_length) {
            uint16_t x_type = PICOPARSE_16(extension_ptr + x_index);
            uint16_t x_len = PICOPARSE_16(extension_ptr + x_index + 2);
            x_index += 4 + x_len;

            if (x_type == 42 && x_len == 4) {
                uint32_t ed_len = PICOPARSE_32(extension_ptr + x_index - 4);
                fprintf(F, "%d(ED: %x),", x_type, ed_len);
            } else {
                fprintf(F, "%d (%d bytes),", x_type, x_len);
            }

            if (x_index > extension_length) {
                fprintf(F, "\n%" PRIu64 ": malformed extensions, require %d bytes, not just %d",
                    picoquic_val64_connection_id(cnx_id), x_index, extension_length);
            }
        }

        fprintf(F, "\n");

        if (x_index < extension_length) {
            fprintf(F, "\n%" PRIu64 ": %d extra bytes at the end of the extensions\n",
                picoquic_val64_connection_id(cnx_id), extension_length - x_index);
        }
    }

    if (ret == -2) {
        fprintf(F, "%" PRIu64 ": Malformed TLS ticket, %d extra bytes.\n",
            picoquic_val64_connection_id(cnx_id), ticket_length - min_length);
    }
}

/*

From Picotls code:
uint64_t time;
uint16_t cipher_suite;
24 bit int = length of ticket;
<TLS ticket>
16 bit length
<resumption secret>

 */

void picoquic_log_picotls_ticket(FILE* F, picoquic_connection_id_t cnx_id,
    uint8_t* ticket, uint16_t ticket_length)
{
    uint64_t ticket_time = 0;
    uint16_t kx_id = 0;
    uint16_t suite_id = 0;
    uint32_t tls_ticket_length = 0;
    uint8_t* tls_ticket_ptr = NULL;
    uint16_t secret_length = 0;
    /* uint8_t* secret_ptr = NULL; */
    uint16_t byte_index = 0;
    uint32_t min_length = 8 + 2 + 3 + 2;
    int ret = 0;

    if (ticket_length < min_length) {
        ret = -1;
    } else {
        ticket_time = PICOPARSE_64(ticket);
        byte_index += 8;
        kx_id = PICOPARSE_16(ticket + byte_index);
        byte_index += 2;
        suite_id = PICOPARSE_16(ticket + byte_index);
        byte_index += 2;
        tls_ticket_length = PICOPARSE_24(ticket + byte_index);
        byte_index += 3;
        min_length += tls_ticket_length;
        if (ticket_length < min_length) {
            ret = -1;
        } else {
            tls_ticket_ptr = &ticket[byte_index];
            byte_index += (uint16_t) tls_ticket_length;

            secret_length = PICOPARSE_16(ticket + byte_index);
            min_length += secret_length;
            if (ticket_length < min_length) {
                ret = -1;
            } else {
                /* secret_ptr = &ticket[byte_index]; */
                if (min_length > ticket_length) {
                    ret = -2;
                }
            }
        }
    }

    fprintf(F, "%" PRIu64 ": ticket time = %" PRIu64 ", kx = %x, suite = %x, %d ticket, %d secret.\n",
        picoquic_val64_connection_id(cnx_id), ticket_time,
        kx_id, suite_id, tls_ticket_length, secret_length);

    if (ret == -1) {
        fprintf(F, "%" PRIu64 ": Malformed PTLS ticket, length = %d, at least %d required.\n",
            picoquic_val64_connection_id(cnx_id), ticket_length, min_length);
    } else {
        if (tls_ticket_length > 0 && tls_ticket_ptr != NULL) {
            picoquic_log_tls_ticket(F, cnx_id, tls_ticket_ptr, (uint16_t) tls_ticket_length);
        }
    }

    if (ret == -2) {
        fprintf(F, "%" PRIu64 ": Malformed PTLS ticket, %d extra bytes.\n",
            picoquic_val64_connection_id(cnx_id), ticket_length - min_length);
    }
}
