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

#ifdef _WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "getopt.h"
#include <WinSock2.h>
#include <Windows.h>
#include <assert.h>
#include <iphlpapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ws2tcpip.h>

#ifndef SOCKET_TYPE
#define SOCKET_TYPE SOCKET
#endif
#ifndef SOCKET_CLOSE
#define SOCKET_CLOSE(x) closesocket(x)
#endif
#ifndef WSA_START_DATA
#define WSA_START_DATA WSADATA
#endif
#ifndef WSA_START
#define WSA_START(x, y) WSAStartup((x), (y))
#endif
#ifndef WSA_LAST_ERROR
#define WSA_LAST_ERROR(x) WSAGetLastError()
#endif
#ifndef socklen_t
#define socklen_t int
#endif

#ifdef _WINDOWS64
static const char* default_server_cert_file = "..\\..\\certs\\cert.pem";
static const char* default_server_key_file = "..\\..\\certs\\key.pem";
#else
static const char* default_server_cert_file = "..\\certs\\cert.pem";
static const char* default_server_key_file = "..\\certs\\key.pem";
#endif

#else /* Linux */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <strings.h>
#include <ctype.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif
#ifndef __USE_POSIX
#define __USE_POSIX
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <fcntl.h>

#ifndef SOCKET_TYPE
#define SOCKET_TYPE int
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_CLOSE
#define SOCKET_CLOSE(x) close(x)
#endif
#ifndef WSA_LAST_ERROR
#define WSA_LAST_ERROR(x) ((long)(x))
#endif

static const char* default_server_cert_file = "certs/cert.pem";
static const char* default_server_key_file = "certs/key.pem";

#endif

#define MIN(a, b) (((a) <= (b)) ? (a) : (b))

static const int default_server_port = 4443;
static const char* default_server_name = "::";
static char* ticket_store_filename = "demo_ticket_store.bin";

static const char* bad_request_message = "<html><head><title>Bad Request</title></head><body>Bad request. Why don't you try \"GET /doc-456789.html\"?</body></html>";

#include "../picoquic/picoquic.h"
#include "../picoquic/picoquic_internal.h"
#include "../picoquic/picosocks.h"
#include "../picoquic/util.h"
#include "../picoquic/plugin.h"

static const char* response_buffer = NULL;
static size_t response_length = 0;

static size_t max_stream_receive_window_size = SIZE_MAX;
static ssize_t repair_receive_window_size = -1L;

uint64_t fixed_cwin = 0;

void print_address(struct sockaddr* address, char* label, picoquic_connection_id_t cnx_id)
{
    char hostname[256];

    const char* x = inet_ntop(address->sa_family,
        (address->sa_family == AF_INET) ? (void*)&(((struct sockaddr_in*)address)->sin_addr) : (void*)&(((struct sockaddr_in6*)address)->sin6_addr),
        hostname, sizeof(hostname));

    printf("%" PRIx64 ": ", picoquic_val64_connection_id(cnx_id));

    if (x != NULL) {
        printf("%s %s, port %d\n", label, x,
            (address->sa_family == AF_INET) ? ((struct sockaddr_in*)address)->sin_port : ((struct sockaddr_in6*)address)->sin6_port);
    } else {
        printf("%s: inet_ntop failed with error # %ld\n", label, WSA_LAST_ERROR(errno));
    }
}

static char* strip_endofline(char* buf, size_t bufmax, char const* line)
{
    for (size_t i = 0; i < bufmax; i++) {
        int c = line[i];

        if (c == 0 || c == '\r' || c == '\n') {
            buf[i] = 0;
            break;
        } else {
            buf[i] = c;
        }
    }

    buf[bufmax - 1] = 0;
    return buf;
}

#define PICOQUIC_FIRST_COMMAND_MAX 128
#define PICOQUIC_FIRST_RESPONSE_MAX (1 << 30)
#define PICOQUIC_DEMO_MAX_PLUGIN_FILES 64

static protoop_id_t set_qlog_file = { .id = "set_qlog_file" };

typedef enum {
    picoquic_first_server_stream_status_none = 0,
    picoquic_first_server_stream_status_receiving,
    picoquic_first_server_stream_status_finished
} picoquic_first_server_stream_status_t;

typedef struct st_picoquic_first_server_stream_ctx_t {
    struct st_picoquic_first_server_stream_ctx_t* next_stream;
    picoquic_first_server_stream_status_t status;
    uint64_t stream_id;
    size_t command_length;
    size_t response_length;
    uint8_t command[PICOQUIC_FIRST_COMMAND_MAX];
} picoquic_first_server_stream_ctx_t;

typedef struct st_picoquic_first_server_callback_ctx_t {
    picoquic_first_server_stream_ctx_t* first_stream;
    size_t buffer_max;
    uint8_t* buffer;
} picoquic_first_server_callback_ctx_t;

static protoop_id_t congestion_set_fixed_cwin = { .id = "congestion_set_fixed_cwin" };

static protoop_id_t window_fec_framework_set_rwin_size = { .id = "window_fec_framework_set_rwin_size" };

/**
 * sets the  fixed cwin assuming the diable_congestion_control plugin is plugged
 */
int set_fixed_cwin(picoquic_cnx_t *cnx, uint64_t cwin) {
    printf("set fixed cwin, cnx = %p\n", cnx);
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &congestion_set_fixed_cwin, NULL, cwin);
    if (ret != 0) {
        fprintf(stdout, "could not set the cwin\n");
    }
    printf("return, cnx = %p\n", cnx);
    return ret;
}
/**
 * sets the rwin size
 * MUST BE CALLED BEFORE DATA ARE ACTUALLY EXCHANGED WITH THE PEER
 */
int fec_set_rwin_size(picoquic_cnx_t *cnx, ssize_t source_rwin_size, ssize_t repair_rwin_size) {
    printf("set rwin size %ld and %ld\n", source_rwin_size, repair_rwin_size);
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &window_fec_framework_set_rwin_size, NULL, source_rwin_size, repair_rwin_size);
    if (ret != 0) {
        fprintf(stdout, "could not send the protected message\n");
    }
    return ret;
}

static picoquic_first_server_callback_ctx_t* first_server_callback_create_context()
{
    picoquic_first_server_callback_ctx_t* ctx = (picoquic_first_server_callback_ctx_t*)
        malloc(sizeof(picoquic_first_server_callback_ctx_t));

    if (ctx != NULL) {
        ctx->first_stream = NULL;
        ctx->buffer = (uint8_t*)malloc(PICOQUIC_FIRST_RESPONSE_MAX);
        if (ctx->buffer == NULL) {
            free(ctx);
            ctx = NULL;
        } else {
            ctx->buffer_max = PICOQUIC_FIRST_RESPONSE_MAX;
        }
    }

    return ctx;
}

static void first_server_callback_delete_context(picoquic_first_server_callback_ctx_t* ctx)
{
    picoquic_first_server_stream_ctx_t* stream_ctx;

    while ((stream_ctx = ctx->first_stream) != NULL) {
        ctx->first_stream = stream_ctx->next_stream;
        free(stream_ctx);
    }

    if (ctx->buffer) {
        free(ctx->buffer);
    }
    free(ctx);
}

static void write_stats(picoquic_cnx_t *cnx, char *filename) {
    if (!filename) return;
    FILE *out = stdout;
    bool file = false;
    if (strcmp(filename, "-")) {
        out = fopen(filename, "w");
        if (!out) {
            fprintf(stderr, "impossible to write stats on file %s\n", filename);
            return;
        }
        file = true;
    }
    plugin_stat_t *stats = malloc(100*sizeof(plugin_stat_t));
    int nstats = picoquic_get_plugin_stats(cnx, &stats, 100);
    printf("%d stats\n", nstats);
    if (nstats != -1) {
        const int size = 300;
        char str[size];
        char buf[size];
        str[0] = '\0';
        str[size-1] = '\0';
        buf[0] = '\0';
        buf[size-1] = '\0';
        for (int i = 0 ; i < nstats ; i++) {
            if (stats[i].pre){
                strcpy(str, "pre");
            } else if (stats[i].post) {
                strcpy(str, "post");
            } else {
                strcpy(str, "replace");
            }
            snprintf(buf, size-1, "%s %s", str, stats[i].protoop_name);
            strncpy(str, buf, size-1);
            if (stats[i].is_param) {
                snprintf(buf, size-1, "%s (param 0x%hx)", str, stats[i].param);
                strncpy(str, buf, size-1);
            }
            snprintf(buf, size-1, "%s (%s)", str, stats[i].pluglet_name);
            strncpy(str, buf, size-1);
            snprintf(buf, size-1, "%s: %lu calls", str, stats[i].count);
            strncpy(str, buf, size-1);
            double average_execution_time = stats[i].count ? (((double) stats[i].total_execution_time)/((double) stats[i].count)) : 0;
            snprintf(buf, size-1, "%s, (avg=%fms, tot=%fms)", str, average_execution_time/1000, ((double) stats[i].total_execution_time)/1000);
            strncpy(str, buf, size-1);
            fprintf(out, "%s\n", str);
        }
    }
    free(stats);
    if (file) fclose(out);
}

static int get_request_length(char *command, size_t command_length)
{
    if (!strstr(command, "doc")) {
        return -1;
    }
    char *start = rindex(command, '-');
    if (!start) {
        return -1;
    }
    start++;
    char buf[11];
    int i;
    for (i = 0; i < 11 && isdigit(start[i]); i++) {
        buf[i] = start[i];
    }
    buf[i] = 0;
    char *error = NULL;
    int ret = strtol(buf, &error, 10);
    if (strlen(error) > 0) {
        return -1;
    }
    return ret;
}

static void first_server_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx)
{
    picoquic_first_server_callback_ctx_t* ctx = (picoquic_first_server_callback_ctx_t*)callback_ctx;
    picoquic_first_server_stream_ctx_t* stream_ctx = NULL;

    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
    picoquic_log_time(stdout, cnx, picoquic_current_time(), "", " : ");
    printf("Server CB, Stream: %" PRIu64 ", %" PRIst " bytes, fin=%d (%s)\n",
        stream_id, length, fin_or_event, picoquic_log_fin_or_event_name(fin_or_event));

    if (fin_or_event == picoquic_callback_close || 
        fin_or_event == picoquic_callback_application_close ||
        fin_or_event == picoquic_callback_stateless_reset) {
        if (ctx != NULL) {
            first_server_callback_delete_context(ctx);
            picoquic_set_callback(cnx, first_server_callback, NULL);
        }
        fflush(stdout);
        return;
    }

    if (fin_or_event == picoquic_callback_challenge_response) {
        fflush(stdout);
        return;
    }

    if (ctx == NULL) {
        picoquic_first_server_callback_ctx_t* new_ctx = first_server_callback_create_context();
        if (new_ctx == NULL) {
            /* cannot handle the connection */
            printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
            printf("Memory error, cannot allocate application context\n");

            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return;
        } else {
            picoquic_set_callback(cnx, first_server_callback, new_ctx);
            ctx = new_ctx;
        }
    }

    stream_ctx = ctx->first_stream;

    /* if stream is already present, check its state. New bytes? */
    while (stream_ctx != NULL && stream_ctx->stream_id != stream_id) {
        stream_ctx = stream_ctx->next_stream;
    }

    if (stream_ctx == NULL) {
        stream_ctx = (picoquic_first_server_stream_ctx_t*)
            malloc(sizeof(picoquic_first_server_stream_ctx_t));
        if (stream_ctx == NULL) {
            /* Could not handle this stream */
            picoquic_reset_stream(cnx, stream_id, 500);
            return;
        } else {
            memset(stream_ctx, 0, sizeof(picoquic_first_server_stream_ctx_t));
            stream_ctx->next_stream = ctx->first_stream;
            ctx->first_stream = stream_ctx;
            stream_ctx->stream_id = stream_id;
        }
    }

    /* verify state and copy data to the stream buffer */
    if (fin_or_event == picoquic_callback_stop_sending) {
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, 0);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Stop Sending Stream: %" PRIu64 ", resetting the local stream.\n",
            stream_id);
        return;
    } else if (fin_or_event == picoquic_callback_stream_reset) {
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, 0);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Reset Stream: %" PRIu64 ", resetting the local stream.\n",
            stream_id);
        return;
    } else if (stream_ctx->status == picoquic_first_server_stream_status_finished || stream_ctx->command_length + length > (PICOQUIC_FIRST_COMMAND_MAX - 1)) {
        if (fin_or_event == picoquic_callback_stream_fin && length == 0) {
            /* no problem, this is fine. */
        } else {
            /* send after fin, or too many bytes => reset! */
            picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_STREAM_STATE_ERROR);
            printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
            printf("Server CB, Stream: %" PRIu64 ", RESET, too long or after FIN\n",
                stream_id);
        }
        return;
    } else if (fin_or_event == picoquic_callback_stream_gap) {
        /* We do not support this, yet */
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_PROTOCOL_VIOLATION);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Stream: %" PRIu64 ", RESET, stream gaps not supported\n", stream_id);
        return;
    } else if (fin_or_event == picoquic_callback_no_event || fin_or_event == picoquic_callback_stream_fin) {
        int crlf_present = 0;

        if (length > 0) {
            memcpy(&stream_ctx->command[stream_ctx->command_length],
                bytes, length);
            stream_ctx->command_length += length;
            for (size_t i = 0; i < length; i++) {
                if (bytes[i] == '\r' || bytes[i] == '\n') {
                    crlf_present = 1;
                    break;
                }
            }
        }

        /* if FIN present, process request through http 0.9 */
        if ((fin_or_event == picoquic_callback_stream_fin || crlf_present != 0) && stream_ctx->response_length == 0) {
            stream_ctx->command[stream_ctx->command_length] = 0;
            /* if data generated, just send it. Otherwise, just FIN the stream. */
            stream_ctx->status = picoquic_first_server_stream_status_finished;

            if (response_length > 0) {
                stream_ctx->response_length = get_request_length((char *) stream_ctx->command, stream_ctx->command_length);
                if (stream_ctx->response_length < 0 || stream_ctx->response_length > response_length) {
                    stream_ctx->response_length = response_length;
                }
                printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
                printf("Server CB, Stream: %" PRIu64 ", Sending %lu-byte static response\n",
                       stream_id, stream_ctx->response_length);
                picoquic_add_to_stream(cnx, stream_ctx->stream_id, (const uint8_t*) response_buffer, stream_ctx->response_length, 1);
            } else {
                char buf[256];
                if (http0dot9_get(stream_ctx->command, stream_ctx->command_length,
                                  ctx->buffer, ctx->buffer_max, &stream_ctx->response_length)
                    != 0) {
                    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
                    printf("Server CB, Stream: %" PRIu64 ", Reply with bad request message after command: %s\n",
                           stream_id, strip_endofline(buf, sizeof(buf), (char *) &stream_ctx->command));

                    // picoquic_reset_stream(cnx, stream_id, 404);

                    (void) picoquic_add_to_stream(cnx, stream_ctx->stream_id, (const uint8_t *) bad_request_message,
                                                  strlen(bad_request_message), 1);
                } else {
                    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
                    printf("Server CB, Stream: %" PRIu64 ", Processing command: %s\n",
                           stream_id, strip_endofline(buf, sizeof(buf), (char *) &stream_ctx->command));
                    picoquic_add_to_stream(cnx, stream_id, ctx->buffer,
                                           stream_ctx->response_length, 1);
                }
            }
        } else if (stream_ctx->response_length == 0) {
            char buf[256];

            printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
            stream_ctx->command[stream_ctx->command_length] = 0;
            printf("Server CB, Stream: %" PRIu64 ", Partial command: %s\n",
                stream_id, strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command));
            fflush(stdout);
        }
    } else {
        /* Unknown event */
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_INTERNAL_ERROR);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Stream: %" PRIu64 ", unexpected event\n", stream_id);
        return;
    }

    /* that's it */
}

int quic_server(const char* server_name, int server_port,
    const char* pem_cert, const char* pem_key,
    int just_once, int do_hrr, cnx_id_cb_fn cnx_id_callback,
    void* cnx_id_callback_ctx, uint8_t reset_seed[PICOQUIC_RESET_SECRET_SIZE],
    int mtu_max, const char** local_plugin_fnames, int local_plugins,
    const char** both_plugin_fnames, int both_plugins, FILE *F_log, FILE *F_tls_secrets, char *qlog_filename, char *stats_filename, bool preload_plugins)
{
    /* Start: start the QUIC process with cert and key files */
    int ret = 0;
    picoquic_quic_t* qserver = NULL;
    picoquic_cnx_t* cnx_server = NULL;
    picoquic_cnx_t* cnx_next = NULL;
    picoquic_path_t* path = NULL;
    picoquic_server_sockets_t server_sockets;
    struct sockaddr_storage addr_from;
    struct sockaddr_storage addr_to;
    unsigned long if_index_to;
    struct sockaddr_storage client_from;
    socklen_t from_length;
    socklen_t to_length;
    uint8_t buffer[1536];
    uint8_t send_buffer[1536];
    size_t send_length = 0;
    picoquic_stateless_packet_t* sp;
    int64_t delay_max = 10000000;
    int new_context_created = 0;

#ifdef STATIC_RESPONSE
    response_buffer = (const char *) malloc(STATIC_RESPONSE);
    if (!response_buffer) {
        fprintf(stderr, "Unable to allocated %d-byte static response\n", STATIC_RESPONSE);
        exit(-1);
    }
    memset((char *) response_buffer, 42, STATIC_RESPONSE);
    response_length  = STATIC_RESPONSE;
#endif

    printf("start server !\n");
    /* Open a UDP socket */
    ret = picoquic_open_server_sockets(&server_sockets, server_port);

    /* Wait for packets and process them */
    if (ret == 0) {
        /* Create QUIC context */
        qserver = picoquic_create(8, pem_cert, pem_key, NULL, NULL, first_server_callback, NULL,
            cnx_id_callback, cnx_id_callback_ctx, reset_seed, picoquic_current_time(), NULL, NULL, NULL, 0, NULL);

        if (qserver == NULL) {
            printf("Could not create server context\n");
            ret = -1;
        } else {
            if (do_hrr != 0) {
                picoquic_set_cookie_mode(qserver, 1);
            }
            qserver->mtu_max = mtu_max;
            /* TODO: add log level, to reduce size in "normal" cases */
            PICOQUIC_SET_LOG(qserver, F_log);
            PICOQUIC_SET_TLS_SECRETS_LOG(qserver, F_tls_secrets);

            /* As we currently do not modify plugins to inject yet, we can store it in the quic structure */
            ret = picoquic_set_plugins_to_inject(qserver, both_plugin_fnames, both_plugins);
            if (ret != 0) {
                printf("Error when setting plugins to inject\n");
            }
        }
    }


    if (preload_plugins) {
        // pre-load a mock connection and insert the plugins
        picoquic_connection_id_t dst;
        dst.id_len = 4;
        picoquic_connection_id_t src;
        src.id_len = 4;
        struct sockaddr_storage a;
        picoquic_cnx_t *tmp_cnx = picoquic_create_cnx(qserver, dst, src, (struct sockaddr *) &a, picoquic_current_time(), 0xff00000b, NULL, NULL, 0);

        if (local_plugins > 0) {
            plugin_insert_plugins_from_fnames(tmp_cnx, local_plugins, (char **) local_plugin_fnames);
        }

        picoquic_delete_cnx(tmp_cnx);
    }


    /* Wait for packets */
    while (ret == 0 && (just_once == 0 || cnx_server == NULL || picoquic_get_cnx_state(cnx_server) != picoquic_state_disconnected)) {
        uint64_t time_before = picoquic_current_time();
        uint64_t current_time = picoquic_current_time();
        int64_t delta_t = picoquic_get_next_wake_delay(qserver, picoquic_current_time(), delay_max);
        int bytes_recv;

        from_length = to_length = sizeof(struct sockaddr_storage);
        if_index_to = 0;

        if (just_once != 0 && delta_t > 10000 && cnx_server != NULL) {
            picoquic_log_congestion_state(F_log, cnx_server, picoquic_current_time());
        }

        bytes_recv = picoquic_select(server_sockets.s_socket, PICOQUIC_NB_SERVER_SOCKETS,
            &addr_from, &from_length,
            &addr_to, &to_length, &if_index_to,
            buffer, sizeof(buffer),
            delta_t, &current_time,
            qserver);

        if (just_once != 0) {
            if (bytes_recv > 0) {
                printf("Select returns %d, from length %u after %d us (wait for %d us)\n",
                    bytes_recv, from_length, (int)(current_time - time_before), (int)delta_t);
                print_address((struct sockaddr*)&addr_from, "recv from:", picoquic_null_connection_id);
            } else {
                printf("Select return %d, after %d us (wait for %d us)\n", bytes_recv,
                    (int)(current_time - time_before), (int)delta_t);
            }
        }

        printf("bytes revc = %d\n", bytes_recv);

        if (bytes_recv < 0) {
            ret = -1;
        } else {
            if (bytes_recv > 0) {
                /* Submit the packet to the server */
                ret = picoquic_incoming_packet(qserver, buffer,
                    (size_t)bytes_recv, (struct sockaddr*)&addr_from,
                    (struct sockaddr*)&addr_to, if_index_to,
                    current_time, &new_context_created);

                if (ret != 0) {
                    ret = 0;
                }

                printf("new context created = %d\n", new_context_created);

                if (new_context_created) {
                    cnx_server = picoquic_get_first_cnx(qserver);

                    /* We first insert all locally asked plugins */
                    if (local_plugins > 0) {
                        printf("%" PRIx64 ": ",
                                picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_server)));
                        plugin_insert_plugins_from_fnames(cnx_server, local_plugins, (char **) local_plugin_fnames);
                    }

                    picoquic_handle_plugin_negotiation(cnx_server);

                    if (qlog_filename) {
                        int qlog_fd = open(qlog_filename, O_WRONLY | O_CREAT | O_TRUNC, 00755);
                        if (qlog_fd != -1) {
                            protoop_prepare_and_run_extern_noparam(cnx_server, &set_qlog_file, NULL, qlog_fd);
                        } else {
                            perror("qlog_fd");
                        }
                    }

                    if (repair_receive_window_size != -1L) {
                        if (max_stream_receive_window_size == -1L) {
                            max_stream_receive_window_size = 2000000;
                        }
                        ret = fec_set_rwin_size(cnx_server, max_stream_receive_window_size, repair_receive_window_size);
                    }

                    if (fixed_cwin != 0) {
                        printf("SET FIXED CWIN TO %lu\n", fixed_cwin);
                        set_fixed_cwin(cnx_server, fixed_cwin);
                    }

                    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_server)));
                    picoquic_log_time(stdout, cnx_server, picoquic_current_time(), "", " : ");
                    printf("Connection established, state = %d, from length: %u\n",
                        picoquic_get_cnx_state(picoquic_get_first_cnx(qserver)), from_length);
                    memset(&client_from, 0, sizeof(client_from));
                    memcpy(&client_from, &addr_from, from_length);

                    print_address((struct sockaddr*)&client_from, "Client address:",
                        picoquic_get_logging_cnxid(cnx_server));
                    picoquic_log_transport_extension(stdout, cnx_server, 1);

                }
            }
            if (ret == 0) {

                uint64_t loop_time = picoquic_current_time();

                while ((sp = picoquic_dequeue_stateless_packet(qserver)) != NULL) {
                    (void) picoquic_send_through_server_sockets(&server_sockets,
                        (struct sockaddr*)&sp->addr_to,
                        (sp->addr_to.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                        (struct sockaddr*)&sp->addr_local,
                        (sp->addr_local.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                        sp->if_index_local,
                        (const char*)sp->bytes, (int)sp->length);

                    /* TODO: log stateless packet */

                    fflush(stdout);

                    picoquic_delete_stateless_packet(sp);
                }

                while (ret == 0 && (cnx_next = picoquic_get_earliest_cnx_to_wake(qserver, loop_time)) != NULL) {
                    ret = picoquic_prepare_packet(cnx_next, picoquic_current_time(),
                        send_buffer, sizeof(send_buffer), &send_length, &path);

                    if (ret == PICOQUIC_ERROR_DISCONNECTED) {
                        ret = 0;

                        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_next)));
                        picoquic_log_time(stdout, cnx_next, picoquic_current_time(), "", " : ");
                        printf("Closed. Retrans= %d, spurious= %d, max sp gap = %d, max sp delay = %d\n",
                            (int)cnx_next->nb_retransmission_total, (int)cnx_next->nb_spurious,
                            (int)cnx_next->path[0]->max_reorder_gap, (int)cnx_next->path[0]->max_spurious_rtt);

                        if (cnx_next == cnx_server) {
                            cnx_server = NULL;
                        }
                        write_stats(cnx_next, stats_filename);
                        picoquic_delete_cnx(cnx_next);

                        fflush(stdout);
                        break;
                    } else if (ret == 0) {
                        int peer_addr_len = 0;
                        struct sockaddr* peer_addr;
                        int local_addr_len = 0;
                        struct sockaddr* local_addr;

                        if (send_length > 0) {
                            if (just_once != 0 ||
                                cnx_next->cnx_state < picoquic_state_client_ready ||
                                cnx_next->cnx_state >= picoquic_state_disconnecting) {
                                printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_next)));
                                printf("Connection state = %d\n",
                                    picoquic_get_cnx_state(cnx_next));
                            }

                            picoquic_get_peer_addr(path, &peer_addr, &peer_addr_len);
                            picoquic_get_local_addr(path, &local_addr, &local_addr_len);

                            /* QDC: I hate having those lines here... But it is the only place to hook before sending... */
                            /* Both Linux and Windows use separate sockets for V4 and V6 */
#ifndef NS3
                            int socket_index = (peer_addr->sa_family == AF_INET) ? 1 : 0;
#else
                            int socket_index = 0;
#endif
                            picoquic_before_sending_packet(cnx_next, server_sockets.s_socket[socket_index]);

                            (void)picoquic_send_through_server_sockets(&server_sockets,
                                peer_addr, peer_addr_len, local_addr, local_addr_len,
                                picoquic_get_local_if_index(path),
                                (const char*)send_buffer, (int)send_length);

                            /* TODO: log sending packet. */
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    }

    printf("Server exit, ret = %d\n", ret);

    /* Clean up */
    if (qserver != NULL) {
        picoquic_free(qserver);
    }

    picoquic_close_server_sockets(&server_sockets);

    return ret;
}

typedef struct st_demo_stream_desc_t {
    uint32_t stream_id;
    uint32_t previous_stream_id;
    char const* doc_name;
    char const* f_name;
    int is_binary;
} demo_stream_desc_t;

static const demo_stream_desc_t test_scenario[] = {
#ifdef PICOQUIC_TEST_AGAINST_ATS
    { 0, 0xFFFFFFFF, "", "slash.html", 0 },
    { 8, 4, "en/latest/", "slash_en_slash_latest.html", 0 }
#else
#ifdef PICOQUIC_TEST_AGAINST_QUICKLY
    { 0, 0xFFFFFFFF, "123.txt", "123.txt", 0 }
#else
    { 0, 0xFFFFFFFF, "index.html", "index.html", 0 },
    { 4, 0, "test.html", "test.html", 0 },
    { 8, 0, "doc-123456.html", "doc-123456.html", 0 },
    { 12, 0, "main.jpg", "main.jpg", 1 },
    { 16, 0, "war-and-peace.txt", "war-and-peace.txt", 0 },
    { 20, 0, "en/latest/", "slash_en_slash_latest.html", 0 },
    { 24, 0, "doc-4000000.html", "doc-4000000.html", 0 }
#endif
#endif
};

static const size_t test_scenario_nb = sizeof(test_scenario) / sizeof(demo_stream_desc_t);

typedef struct st_picoquic_first_client_stream_ctx_t {
    struct st_picoquic_first_client_stream_ctx_t* next_stream;
    uint32_t stream_id;
    uint8_t command[PICOQUIC_FIRST_COMMAND_MAX + 1]; /* starts with "GET " */
    size_t received_length;
    FILE* F; /* NULL if stream is closed. */
} picoquic_first_client_stream_ctx_t;

typedef struct st_picoquic_first_client_callback_ctx_t {
    demo_stream_desc_t* demo_stream;
    size_t nb_demo_streams;

    struct st_picoquic_first_client_stream_ctx_t* first_stream;
    int nb_open_streams;
    uint32_t nb_client_streams;
    uint64_t last_interaction_time;
    int progress_observed;
    struct timeval tv_start;
    struct timeval tv_end;
    bool stream_ok;
    int n_stream_ok;
    bool only_get;
} picoquic_first_client_callback_ctx_t;

static void demo_client_open_stream(picoquic_cnx_t* cnx,
    picoquic_first_client_callback_ctx_t* ctx,
    uint32_t stream_id, char const* text, size_t text_len, char const* fname, int is_binary)
{
    picoquic_first_client_stream_ctx_t* stream_ctx = (picoquic_first_client_stream_ctx_t*)
        malloc(sizeof(picoquic_first_client_stream_ctx_t));

    if (stream_id == 4 && ctx->only_get) {
        gettimeofday(&ctx->tv_start, NULL);
    }

    if (stream_ctx == NULL) {
        fprintf(stdout, "Memory error!\n");
    } else {
        int ret = 0;
        fprintf(stdout, "Opening stream %u to GET /%s\n", stream_id, text);

        memset(stream_ctx, 0, sizeof(picoquic_first_client_stream_ctx_t));
        stream_ctx->command[0] = 'G';
        stream_ctx->command[1] = 'E';
        stream_ctx->command[2] = 'T';
        stream_ctx->command[3] = ' ';
        stream_ctx->command[4] = '/';
        if (text_len > 0) {
            memcpy(&stream_ctx->command[5], text, text_len);
        }
        stream_ctx->command[text_len + 5] = '\r';
        stream_ctx->command[text_len + 6] = '\n';
        stream_ctx->command[text_len + 7] = 0;
        stream_ctx->stream_id = stream_id;

        stream_ctx->next_stream = ctx->first_stream;
        ctx->first_stream = stream_ctx;

#ifdef _WINDOWS
        if (fopen_s(&stream_ctx->F, fname, (is_binary == 0) ? "w" : "wb") != 0) {
            ret = -1;
        }
#else
        stream_ctx->F = fopen(fname, (is_binary == 0) ? "w" : "wb");
        if (stream_ctx->F == NULL) {
            ret = -1;
        }
#endif
        if (ret != 0) {
            fprintf(stdout, "Cannot create file: %s\n", fname);
        } else {
            ctx->nb_open_streams++;
            ctx->nb_client_streams++;
        }

        if (stream_ctx->stream_id == 1) {
            /* Horrible hack to test sending in three blocks */
            (void)picoquic_add_to_stream(cnx, stream_ctx->stream_id, stream_ctx->command,
                5, 0);
            (void)picoquic_add_to_stream(cnx, stream_ctx->stream_id, &stream_ctx->command[5],
                text_len, 0);
            (void)picoquic_add_to_stream(cnx, stream_ctx->stream_id, &stream_ctx->command[5 + text_len],
                2, 1);
        } else {
            (void)picoquic_add_to_stream(cnx, stream_ctx->stream_id, stream_ctx->command,
                text_len + 7, 1);
        }
    }
}

static void demo_client_start_streams(picoquic_cnx_t* cnx,
    picoquic_first_client_callback_ctx_t* ctx, uint64_t fin_stream_id)
{
    for (size_t i = 0; i < ctx->nb_demo_streams; i++) {
        if (ctx->demo_stream[i].previous_stream_id == fin_stream_id) {
            demo_client_open_stream(cnx, ctx, ctx->demo_stream[i].stream_id,
                ctx->demo_stream[i].doc_name, strlen(ctx->demo_stream[i].doc_name),
                ctx->demo_stream[i].f_name,
                ctx->demo_stream[i].is_binary);
        }
    }
}

static void demo_client_start_new_streams(picoquic_cnx_t* cnx,
    picoquic_first_client_callback_ctx_t* ctx, uint64_t fin_stream_id, int start_idx)
{
    for (size_t i = start_idx; i < ctx->nb_demo_streams; i++) {
        if (ctx->demo_stream[i].previous_stream_id == fin_stream_id) {
            demo_client_open_stream(cnx, ctx, ctx->demo_stream[i].stream_id,
                ctx->demo_stream[i].doc_name, strlen(ctx->demo_stream[i].doc_name),
                ctx->demo_stream[i].f_name,
                ctx->demo_stream[i].is_binary);
        }
    }
}

static void first_client_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx)
{
    uint64_t fin_stream_id = 0xFFFFFFFF;

    picoquic_first_client_callback_ctx_t* ctx = (picoquic_first_client_callback_ctx_t*)callback_ctx;
    picoquic_first_client_stream_ctx_t* stream_ctx = ctx->first_stream;

    ctx->last_interaction_time = picoquic_current_time();
    ctx->progress_observed = 1;

    if (fin_or_event == picoquic_callback_close ||
        fin_or_event == picoquic_callback_application_close ||
        fin_or_event == picoquic_callback_stateless_reset) {
        if (fin_or_event == picoquic_callback_application_close) {
            fprintf(stdout, "Received a request to close the application.\n");
        } else if (fin_or_event == picoquic_callback_stateless_reset) {
            fprintf(stdout, "Received a stateless reset.\n");
        } else {
            fprintf(stdout, "Received a request to close the connection.\n");
        }

        while (stream_ctx != NULL) {
            if (stream_ctx->F != NULL) {
                fclose(stream_ctx->F);
                stream_ctx->F = NULL;
                ctx->nb_open_streams--;

                fprintf(stdout, "On stream %u, command: %s stopped after %d bytes\n",
                    stream_ctx->stream_id, stream_ctx->command, (int)stream_ctx->received_length);
            }
            stream_ctx = stream_ctx->next_stream;
        }

        return;
    }
    fprintf(stream_ctx->F ? stream_ctx->F : stdout, "EVENT::{\"time\": %ld, \"type\": \"stream_deliver\", \"range\": [%lu, %lu]}\n", picoquic_current_time(), stream_ctx->received_length, length);

    /* if stream is already present, check its state. New bytes? */
    while (stream_ctx != NULL && stream_ctx->stream_id != stream_id) {
        stream_ctx = stream_ctx->next_stream;
    }

    if (stream_ctx == NULL || stream_ctx->F == NULL) {
        /* Unexpected stream. */
        picoquic_reset_stream(cnx, stream_id, 0);
        return;
    } else if (fin_or_event == picoquic_callback_stream_reset) {
        picoquic_reset_stream(cnx, stream_id, 0);

        if (stream_ctx->F != NULL) {
            char buf[256];

            fclose(stream_ctx->F);
            stream_ctx->F = NULL;
            ctx->nb_open_streams--;

            fprintf(stdout, "Reset received on stream %u, command: %s, after %d bytes\n",
                stream_ctx->stream_id,
                strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command),
                (int)stream_ctx->received_length);
        }
        return;
    } else if (fin_or_event == picoquic_callback_stop_sending) {
        char buf[256];
        picoquic_reset_stream(cnx, stream_id, 0);

        fprintf(stdout, "Stop sending received on stream %u, command: %s\n",
            stream_ctx->stream_id,
            strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command));
        return;
    } else if (fin_or_event == picoquic_callback_stream_gap) {
        /* We do not support this, yet */
        picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_PROTOCOL_VIOLATION);
        return;
    } else if (fin_or_event == picoquic_callback_no_event || fin_or_event == picoquic_callback_stream_fin) {
        if (length > 0) {
            (void)fwrite(bytes, 1, length, stream_ctx->F);
            stream_ctx->received_length += length;
        }

        /* if FIN present, process request through http 0.9 */
        if (fin_or_event == picoquic_callback_stream_fin) {
            char buf[256];
            if (stream_id % 4  == 0) {
                gettimeofday(&ctx->tv_end, NULL);
                ctx->stream_ok = true;
                ctx->n_stream_ok++;
            }
            /* if data generated, just send it. Otherwise, just FIN the stream. */
            fclose(stream_ctx->F);
            stream_ctx->F = NULL;
            ctx->nb_open_streams--;
            fin_stream_id = stream_id;

            fprintf(stdout, "Received file %s, after %d bytes, closing stream %u\n",
                strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command[4]),
                (int)stream_ctx->received_length, stream_ctx->stream_id);
        }
    }

    if (fin_stream_id != 0xFFFFFFFF) {
        demo_client_start_streams(cnx, ctx, fin_stream_id);
    }

    /* that's it */
}

#define PICOQUIC_DEMO_CLIENT_MAX_RECEIVE_BATCH 4

int quic_client(const char* ip_address_text, int server_port, const char * sni, 
    const char * root_crt,
    uint32_t proposed_version, int force_zero_share, int mtu_max, FILE* F_log, FILE* F_tls_secrets,
    const char** local_plugin_fnames, int local_plugins,
    int get_size, int only_stream_4, char *qlog_filename, char *plugin_store_path, char *stats_filename,
    int n_requests, uint64_t requests_interval_microsec)
{
    /* Start: start the QUIC process with cert and key files */
    int ret = 0;
    picoquic_quic_t* qclient = NULL;
    picoquic_cnx_t* cnx_client = NULL;
    picoquic_path_t* path = NULL;
    picoquic_first_client_callback_ctx_t callback_ctx;
    SOCKET_TYPE fd = INVALID_SOCKET;
    struct sockaddr_storage server_address;
    struct sockaddr_storage packet_from;
    struct sockaddr_storage packet_to;
    unsigned long if_index_to;
    socklen_t from_length;
    socklen_t to_length;
    int server_addr_length = 0;
    uint8_t buffer[1536];
    uint8_t send_buffer[1536];
    size_t send_length = 0;
    int bytes_sent;
    uint64_t current_time = 0;
    int client_ready_loop = 0;
    int client_receive_loop = 0;
    int established = 0;
    int is_name = 0;
    int64_t delay_max = 10000000;
    int64_t delta_t = 0;
    int notified_ready = 0;
    const char* alpn = (proposed_version == 0xFF00000D)?"hq-13":"hq-14";
    int zero_rtt_available = 0;
    int new_context_created = 0;
    char buf[25];
    int waiting_transport_parameters = 1;
    int current_requests = 0;
    uint64_t last_performed_request = 0;

    memset(&callback_ctx, 0, sizeof(picoquic_first_client_callback_ctx_t));

    ret = picoquic_get_server_address(ip_address_text, server_port, &server_address, &server_addr_length, &is_name);
    if (sni == NULL && is_name != 0) {
        sni = ip_address_text;
    }

    /* Open a UDP socket */

    if (ret == 0) {
        /* Make the most possible flexible socket */
        fd = socket(/*server_address.ss_family*/DEFAULT_SOCK_AF, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == INVALID_SOCKET) {
            ret = -1;
        } else if (DEFAULT_SOCK_AF == AF_INET6) {
            int val = 1;
            ret = setsockopt(fd, IPPROTO_IPV6, IPV6_DONTFRAG, &val, sizeof(val));
            if (ret != 0) {
                perror("setsockopt IPV6_DONTFRAG");
            }
        }
    }

    /* QDC: please fixme please */
#ifdef _WINDOWS
    int option_value = 1;
    if (sock_af[i] == AF_INET6) {
        ret = setsockopt(sockets->s_socket[i], IPPROTO_IPV6, IPV6_PKTINFO, (char*)&option_value, sizeof(int));
    }
    else {
        ret = setsockopt(sockets->s_socket[i], IPPROTO_IP, IP_PKTINFO, (char*)&option_value, sizeof(int));
    }
#else
    int val = 1;
    ret = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, (char*)&val, sizeof(int));
#ifdef IP_PKTINFO
    ret = setsockopt(fd, IPPROTO_IP, IP_PKTINFO, (char*)&val, sizeof(int));
#else
    /* The IP_PKTINFO structure is not defined on BSD */
    ret = setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, (char*)&val, sizeof(int));
#endif
#endif

    /* Create QUIC context */
    current_time = picoquic_current_time();
    callback_ctx.last_interaction_time = current_time;

    if (ret == 0) {
        qclient = picoquic_create(8, NULL, NULL, root_crt, alpn, NULL, NULL, NULL, NULL, NULL, current_time, NULL, ticket_store_filename, NULL, 0, plugin_store_path);

        if (qclient == NULL) {
            ret = -1;
        } else {
            if (force_zero_share) {
                qclient->flags |= picoquic_context_client_zero_share;
            }
            qclient->mtu_max = mtu_max;

            PICOQUIC_SET_LOG(qclient, F_log);
            PICOQUIC_SET_TLS_SECRETS_LOG(qclient, F_tls_secrets);

            if (sni == NULL) {
                /* Standard verifier would crash */
                fprintf(stdout, "No server name specified, certificate will not be verified.\n");
                if (F_log && F_log != stdout && F_log != stderr)
                {
                    fprintf(F_log, "No server name specified, certificate will not be verified.\n");
                }
                picoquic_set_null_verifier(qclient);
            }
            else if (root_crt == NULL) {

                /* Standard verifier would crash */
                fprintf(stdout, "No root crt list specified, certificate will not be verified.\n");
                if (F_log && F_log != stdout && F_log != stderr)
                {
                    fprintf(F_log, "No root crt list specified, certificate will not be verified.\n");
                }
                picoquic_set_null_verifier(qclient);
            }
        }
    }

    /* Create the client connection */
    if (ret == 0) {
        picoquic_tp_t tp;
        memset(&tp, 0, sizeof(picoquic_tp_t));
        picoquic_init_transport_parameters(&tp, 1);
        // ensure that the receive window is respected at the beginning of the transfer
        tp.initial_max_stream_data_bidi_local = MIN(tp.initial_max_stream_data_bidi_local, max_stream_receive_window_size);
        tp.initial_max_stream_data_bidi_remote = MIN(tp.initial_max_stream_data_bidi_remote, max_stream_receive_window_size);
        tp.initial_max_stream_data_uni = MIN(tp.initial_max_stream_data_uni, max_stream_receive_window_size);
        /* Create a client connection */
        cnx_client = picoquic_create_cnx_with_transport_parameters(qclient, picoquic_null_connection_id, picoquic_null_connection_id,
            (struct sockaddr*)&server_address, current_time,
            proposed_version, sni, alpn, 1, tp);

        if (cnx_client == NULL) {
            ret = -1;
        }
        else {
            cnx_client->max_stream_receive_window_size = max_stream_receive_window_size;
            if (local_plugins > 0) {
                printf("%" PRIx64 ": ",
                        picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_client)));
                plugin_insert_plugins_from_fnames(cnx_client, local_plugins, (char **) local_plugin_fnames);
            }

            if (qlog_filename) {
                int qlog_fd = open(qlog_filename, O_WRONLY | O_CREAT | O_TRUNC, 00755);
                if (qlog_fd != -1) {
                    protoop_prepare_and_run_extern_noparam(cnx_client, &set_qlog_file, NULL, qlog_fd);
                } else {
                    perror("qlog_fd");
                }
            }

            printf("before set rwin\n");

            if (repair_receive_window_size != -1L) {
                if (max_stream_receive_window_size == -1L) {
                    printf("you must set the stream receive window size if you set the repair receive window size\n");
                    ret = -1;
                } else {
                    printf("setting rwin\n");
                    ret = fec_set_rwin_size(cnx_client, max_stream_receive_window_size, repair_receive_window_size);
                    printf("done, ret = %d\n", ret);
                }
            }

            if (ret == 0) {
                picoquic_set_callback(cnx_client, first_client_callback, &callback_ctx);

                ret = picoquic_start_client_cnx(cnx_client);
            }

            printf("rwin set, ret = %d\n", ret);
            if (ret == 0) {
                if (picoquic_is_0rtt_available(cnx_client) && (proposed_version & 0x0a0a0a0a) != 0x0a0a0a0a) {
                    zero_rtt_available = 1;

                    /* Queue a simple frame to perform 0-RTT test */
                    /* Start the download scenario */
                    /* If get_size is greater than 0, select it */
                    if (get_size <= 0) {
                        callback_ctx.demo_stream = test_scenario;
                        callback_ctx.nb_demo_streams = test_scenario_nb;
                    } else {
                        sprintf(buf, "doc-%d.html", get_size);
                        if (only_stream_4) {
                            callback_ctx.demo_stream =  (demo_stream_desc_t []) {{ 4, 0xFFFFFFFF, buf, "/dev/null", 0 }};
                            callback_ctx.nb_demo_streams = 1;
                        } else {
                            callback_ctx.demo_stream =  (demo_stream_desc_t []) {{ 0, 0xFFFFFFFF, "index.html", "/dev/null", 0 }, { 4, 0, buf, "/dev/null", 0 }};
                            callback_ctx.nb_demo_streams = 2;
                        }
                        gettimeofday(&callback_ctx.tv_start, NULL);
                        callback_ctx.stream_ok = false;
                        callback_ctx.only_get = true;
                    }

                    demo_client_start_streams(cnx_client, &callback_ctx, 0xFFFFFFFF);
                }

                ret = picoquic_prepare_packet(cnx_client, picoquic_current_time(),
                    send_buffer, sizeof(send_buffer), &send_length, &path);

                if (ret == 0 && send_length > 0) {
                    /* QDC: I hate having this line here... But it is the only place to hook before sending... */
                    picoquic_before_sending_packet(cnx_client, fd);
                    /* The first packet must be a sendto, next ones, not necessarily! */
                    bytes_sent = sendto(fd, send_buffer, (int)send_length, 0,
                        (struct sockaddr*)&server_address, server_addr_length);

                    if (F_log != NULL) {
                        if (bytes_sent > 0)
                        {
                            picoquic_log_packet_address(F_log, 
                                picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_client)),
                                cnx_client, (struct sockaddr*)&server_address, 0, bytes_sent, picoquic_current_time());
                        }
                        else {
                            fprintf(F_log, "Cannot send first packet to server, returns %d\n", bytes_sent);
                            ret = -1;
                        }
                    }
                }
            }
        }
    }

    /* Wait for packets */
    while (ret == 0 && picoquic_get_cnx_state(cnx_client) != picoquic_state_disconnected) {
        int bytes_recv;

        from_length = to_length = sizeof(struct sockaddr_storage);

        uint64_t select_time = picoquic_current_time();
        bytes_recv = picoquic_select(&fd, 1, &packet_from, &from_length,
            &packet_to, &to_length, &if_index_to,
            buffer, sizeof(buffer),
            delta_t,
            &current_time,
            qclient);

        if (bytes_recv != 0) {
            if (F_log != NULL) {
                fprintf(F_log, "Select returns %d, from length %u, after %d (delta_t was %d)\n", bytes_recv, from_length, current_time - select_time, delta_t);
            }

            if (bytes_recv > 0 && F_log != NULL)
            {
                picoquic_log_packet_address(F_log,
                    picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_client)),
                    cnx_client, (struct sockaddr*)&server_address, 1, bytes_recv, picoquic_current_time());
            }
        }

        if (bytes_recv < 0) {
            ret = -1;
        } else {
            if (bytes_recv > 0) {
                /* Submit the packet to the client */
                ret = picoquic_incoming_packet(qclient, buffer,
                    (size_t)bytes_recv, (struct sockaddr*)&packet_from,
                    (struct sockaddr*)&packet_to, if_index_to,
                    picoquic_current_time(), &new_context_created);
                client_receive_loop++;

                picoquic_log_processing(F_log, cnx_client, bytes_recv, ret);

                if (picoquic_get_cnx_state(cnx_client) == picoquic_state_client_almost_ready && notified_ready == 0) {
                    if (picoquic_tls_is_psk_handshake(cnx_client)) {
                        fprintf(stdout, "The session was properly resumed!\n");
                        if (F_log && F_log != stdout && F_log != stderr) {
                            fprintf(F_log, "The session was properly resumed!\n");
                        }
                    }
                    fprintf(stdout, "Almost ready!\n\n");
                    notified_ready = 1;
                }

                if (waiting_transport_parameters && cnx_client->remote_parameters_received) {
                    picoquic_handle_plugin_negotiation(cnx_client);
                    waiting_transport_parameters = 0;
                }

                if (ret != 0) {
                    picoquic_log_error_packet(F_log, buffer, (size_t)bytes_recv, ret);
                }

                delta_t = 0;
            }

            /* In normal circumstances, the code waits until all packets in the receive
             * queue have been processed before sending new packets. However, if the server
             * is sending lots and lots of data this can lead to the client not getting
             * the occasion to send acknowledgements. The server will start retransmissions,
             * and may eventually drop the connection for lack of acks. So we limit
             * the number of packets that can be received before sending responses. */

            if (bytes_recv == 0 || (ret == 0 && client_receive_loop > PICOQUIC_DEMO_CLIENT_MAX_RECEIVE_BATCH)) {
                client_receive_loop = 0;

                if (ret == 0 && picoquic_get_cnx_state(cnx_client) == picoquic_state_client_ready) {
                    if (established == 0) {
                        picoquic_log_transport_extension(F_log, cnx_client, 0);
                        printf("Connection established. Version = %x, I-CID: %llx\n",
                            picoquic_supported_versions[cnx_client->version_index].version,
                            (unsigned long long)picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_client)));
                        established = 1;

                        if (fixed_cwin != 0) {
                            set_fixed_cwin(cnx_client, fixed_cwin);
                        }

                        if (zero_rtt_available == 0) {
                            /* Start the download scenario */
                            /* If get_size is greater than 0, select it */
                            if (get_size <= 0) {
                                callback_ctx.demo_stream = test_scenario;
                                callback_ctx.nb_demo_streams = test_scenario_nb;
                            } else {
                                sprintf(buf, "doc-%d.html", get_size);
                                demo_stream_desc_t *tab = calloc(sizeof(demo_stream_desc_t), (only_stream_4 ? 1 : 2));
                                if (only_stream_4) {
                                    if (!tab) {
                                        printf("error malloc");
                                        exit(-1);
                                    }
                                    tab->stream_id = 4;
                                    tab->previous_stream_id = 0xFFFFFFFF;
                                    tab->doc_name = buf;
                                    tab->f_name = "/dev/null";
                                    tab->is_binary = 0;
                                    callback_ctx.demo_stream = tab;
                                    callback_ctx.nb_demo_streams = 1;
                                } else {
                                    demo_stream_desc_t stream_1 = { 0, 0xFFFFFFFF, "index.html", "/dev/null", 0 };
                                    demo_stream_desc_t stream_2 = { 4, 0, buf, "/dev/null", 0 };
                                    tab[0] = stream_1;
                                    tab[1] = stream_2;
                                    callback_ctx.demo_stream =  tab;
                                    callback_ctx.nb_demo_streams = 2;
                                }
                                callback_ctx.stream_ok = false;
                                callback_ctx.only_get = true;
                            }

                            demo_client_start_streams(cnx_client, &callback_ctx, 0xFFFFFFFF);
                            current_requests++;
                            last_performed_request = current_time;
                        }
                    } else if (current_requests < n_requests && current_time - last_performed_request >= requests_interval_microsec) {
                        // add a new request when needed

                        sprintf(buf, "doc-%d.html", get_size);
                        demo_stream_desc_t *new_tab = realloc(callback_ctx.demo_stream, sizeof(demo_stream_desc_t)*(callback_ctx.nb_demo_streams + 1));
                        if (!new_tab) {
                            printf("error realloc");
                            exit(-1);
                        }
                        char *new_buf = calloc(1, 100);
                        if (!new_buf) {
                            printf("error calloc\n");
                            exit(-1);
                        }
                        sprintf(new_buf, "doc-%d.html", get_size);
                        demo_stream_desc_t *tab = &new_tab[callback_ctx.nb_demo_streams];
                        memset(tab, 0, sizeof(demo_stream_desc_t));
                        tab->stream_id = (callback_ctx.nb_demo_streams + 1) * 4;
                        tab->previous_stream_id = 0xFFFFFFFF;//new_tab[callback_ctx.nb_demo_streams-1].stream_id;
                        tab->doc_name = new_buf;
                        tab->f_name = "/dev/null";
                        tab->is_binary = 0;
                        callback_ctx.demo_stream = new_tab;
                        callback_ctx.nb_demo_streams++;

                        callback_ctx.stream_ok = false;
                        callback_ctx.only_get = true;
                        current_requests++;
                        last_performed_request = current_time;
                        demo_client_start_new_streams(cnx_client, &callback_ctx, 0xFFFFFFFF, callback_ctx.nb_demo_streams - 1);
                    }

                    client_ready_loop++;

                    if ((bytes_recv == 0 || client_ready_loop > 4) && picoquic_is_cnx_backlog_empty(cnx_client)) {
                        if (current_requests == n_requests && callback_ctx.nb_open_streams == 0) {
                            if (cnx_client->nb_zero_rtt_sent != 0) {
                                fprintf(stdout, "Out of %u zero RTT packets, %u were acked by the server.\n",
                                    cnx_client->nb_zero_rtt_sent, cnx_client->nb_zero_rtt_acked);
                                if (F_log && F_log != stdout && F_log != stderr)
                                {
                                    fprintf(F_log, "Out of %u zero RTT packets, %u were acked by the server.\n",
                                        cnx_client->nb_zero_rtt_sent, cnx_client->nb_zero_rtt_acked);
                                }
                            }
                            fprintf(stdout, "All done, Closing the connection.\n");
                            if (F_log && F_log != stdout && F_log != stderr)
                            {
                                fprintf(F_log, "All done, Closing the connection.\n");
                            }
                            if (get_size > 0) {
                                free((void *)callback_ctx.demo_stream);
                            }
                            ret = picoquic_close(cnx_client, 0);
                        } else if (
                            picoquic_current_time() > callback_ctx.last_interaction_time && picoquic_current_time() - callback_ctx.last_interaction_time > 10000000ull) {
                            fprintf(stdout, "No progress for 10 seconds. Closing. \n");
                            if (F_log && F_log != stdout && F_log != stderr)
                            {
                                fprintf(F_log, "No progress for 10 seconds. Closing. \n");
                            }
                            ret = picoquic_close(cnx_client, 0);
                        }
                    }
                }

                if (ret == 0) {
                    send_length = PICOQUIC_MAX_PACKET_SIZE;

                    ret = picoquic_prepare_packet(cnx_client, picoquic_current_time(),
                        send_buffer, sizeof(send_buffer), &send_length, &path);

                    if (ret == 0 && send_length > 0) {
                        int peer_addr_len = 0;
                        struct sockaddr* peer_addr;
                        int local_addr_len = 0;
                        struct sockaddr* local_addr;

                        /* QDC: I hate having this line here... But it is the only place to hook before sending... */
                        picoquic_before_sending_packet(cnx_client, fd);

                        picoquic_get_peer_addr(path, &peer_addr, &peer_addr_len);
                        picoquic_get_local_addr(path, &local_addr, &local_addr_len);

                        bytes_sent = picoquic_sendmsg(fd, peer_addr, peer_addr_len, local_addr,
                            local_addr_len, picoquic_get_local_if_index(path),
                            (const char *) send_buffer, (int) send_length);

                        picoquic_log_packet_address(F_log,
                            picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_client)),
                            cnx_client, (struct sockaddr*)&server_address, 0, bytes_sent, picoquic_current_time());
                    }
                }
                uint64_t next_sending = last_performed_request + requests_interval_microsec;
                if (current_requests+1 < n_requests) {
                    int64_t nwd = picoquic_get_next_wake_delay(qclient, picoquic_current_time(), delay_max);
                    delta_t = MIN(nwd, (next_sending < current_time) ? 0 : (next_sending - current_time));
                    printf("requests, delta_t = %ld, nwd = %ld, next_sending = %ld\n", delta_t, nwd, next_sending);
                } else {
                    delta_t = picoquic_get_next_wake_delay(qclient, picoquic_current_time(), delay_max);
                    printf("else, delta_t = %ld\n", delta_t);
                }
            }
        }
    }

    /* Clean up */
    if (qclient != NULL) {
        uint8_t* ticket;
        uint16_t ticket_length;

        if (sni != NULL && 0 == picoquic_get_ticket(qclient->p_first_ticket, picoquic_current_time(), sni, (uint16_t)strlen(sni), alpn, (uint16_t)strlen(alpn), &ticket, &ticket_length) && F_log) {
            fprintf(F_log, "Received ticket from %s:\n", sni);
            picoquic_log_picotls_ticket(F_log, picoquic_null_connection_id, ticket, ticket_length);
        }

        if (picoquic_save_tickets(qclient->p_first_ticket, picoquic_current_time(), ticket_store_filename) != 0) {
            fprintf(stderr, "Could not store the saved session tickets.\n");
        }
        write_stats(cnx_client, stats_filename);
        picoquic_free(qclient);
    }

    if (fd != INVALID_SOCKET) {
        SOCKET_CLOSE(fd);
    }

    /* At the end, if we performed a single get, print the time */
    if (get_size > 0) {
        if (callback_ctx.stream_ok && callback_ctx.n_stream_ok == (only_stream_4 ? n_requests : (n_requests + 1))) {
            int time_us = (callback_ctx.tv_end.tv_sec - callback_ctx.tv_start.tv_sec) * 1000000 + callback_ctx.tv_end.tv_usec - callback_ctx.tv_start.tv_usec;
            printf("%d.%03d ms\n", time_us / 1000, time_us % 1000);
        } else {
            printf("n stream ok = %d\n", callback_ctx.n_stream_ok);
            printf("-1.0\n");
        }
    }
    return ret;
}

uint32_t parse_target_version(char const* v_arg)
{
    /* Expect the version to be encoded in base 16 */
    uint32_t v = 0;
    char const* x = v_arg;

    while (*x != 0) {
        int c = *x;

        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'a' && c <= 'f') {
            c -= 'a';
            c += 10;
        } else if (c >= 'A' && c <= 'F') {
            c -= 'A';
            c += 10;
        } else {
            v = 0;
            break;
        }
        v *= 16;
        v += c;
        x++;
    }

    return v;
}

void usage()
{
    fprintf(stderr, "PicoQUIC demo client and server\n");
    fprintf(stderr, "Usage: picoquicdemo [server_name [port]] <options>\n");
    fprintf(stderr, "  For the client mode, specify sever_name and port.\n");
    fprintf(stderr, "  For the server mode, use -p to specify the port.\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c file               cert file (default: %s)\n", default_server_cert_file);
    fprintf(stderr, "  -k file               key file (default: %s)\n", default_server_key_file);
    fprintf(stderr, "  -G size               GET size (default: -1)\n");
    fprintf(stderr, "  -4                    if -G is set, only use a request through the stream 4 instead of 0 then 4\n");
    fprintf(stderr, "  -P file               locally injected plugin file (default: NULL). Do not require peer support. Can be used several times to load several plugins.\n");
    fprintf(stderr, "  -C directory          directory containing the cached plugins requiring support from both peers (default: NULL). Only for client.\n");
    fprintf(stderr, "  -Q file               plugin file to be injected at both side (default: NULL). Can be used several times to require several plugins. Only for server.\n");
    fprintf(stderr, "  -p port               server port (default: %d)\n", default_server_port);
    fprintf(stderr, "  -n sni                sni (default: server name)\n");
    fprintf(stderr, "  -t file               root trust file\n");
    fprintf(stderr, "  -R                    enforce 1RTT\n");
    fprintf(stderr, "  -X file               export the TLS secrets in the specified file\n");
    fprintf(stderr, "  -L                    if server, preload the specified protocol plugins (avoids latency on the first connection)\n");
    fprintf(stderr, "  -1                    Once\n");
    fprintf(stderr, "  -r                    Do Reset Request\n");
    fprintf(stderr, "  -s <64b 64b>          Reset seed\n");
    fprintf(stderr, "  -i <src mask value>   Connection ID modification: (src & ~mask) || val\n");
    fprintf(stderr, "                        Implies unconditional server cnx_id xmit\n");
    fprintf(stderr, "                          where <src> is int:\n");
    fprintf(stderr, "                            0: picoquic_cnx_id_random\n");
    fprintf(stderr, "                            1: picoquic_cnx_id_remote (client)\n");
    fprintf(stderr, "  -v version            Version proposed by client, e.g. -v ff00000a\n");
    fprintf(stderr, "  -z                    Set TLS zero share behavior on client, to force HRR.\n");
    fprintf(stderr, "  -l file               Log file\n");
    fprintf(stderr, "  -m mtu_max            Largest mtu value that can be tried for discovery\n");
    fprintf(stderr, "  -q output.qlog        qlog output file\n");
    fprintf(stderr, "  -S filename           if set, write plugin statistics in the specified file (- for stdout)\n");
    fprintf(stderr, "  -w stream_rwin_max    sets the maximum value in bytes for the size of the streams receive window\n");
    fprintf(stderr, "  -N n_requests         if -G is set, specifies how much time to perform the same request\n");
    fprintf(stderr, "  -I interval_microsec  if -G is set and -N > 1n specifies the interval between each request\n");
    fprintf(stderr, "  -W cwin               sets the congestion window\n");
    fprintf(stderr, "  -F repair_receive_window_size               sets the size of the buffer allocated to store repair symbols when using FEC\n");
    fprintf(stderr, "  -h                    This help message\n");
    exit(1);
}

enum picoquic_cnx_id_select {
    picoquic_cnx_id_random = 0,
    picoquic_cnx_id_remote = 1
};

typedef struct {
    enum picoquic_cnx_id_select cnx_id_select;
    picoquic_connection_id_t cnx_id_mask;
    picoquic_connection_id_t cnx_id_val;
} cnx_id_callback_ctx_t;

static void cnx_id_callback(picoquic_connection_id_t cnx_id_local, picoquic_connection_id_t cnx_id_remote, void* cnx_id_callback_ctx, 
    picoquic_connection_id_t * cnx_id_returned)
{
    uint64_t val64;
    cnx_id_callback_ctx_t* ctx = (cnx_id_callback_ctx_t*)cnx_id_callback_ctx;

    if (ctx->cnx_id_select == picoquic_cnx_id_remote)
        cnx_id_local = cnx_id_remote;

    /* TODO: replace with encrypted value when moving to 17 byte CID */
    val64 = (picoquic_val64_connection_id(cnx_id_local) & picoquic_val64_connection_id(ctx->cnx_id_mask)) |
        picoquic_val64_connection_id(ctx->cnx_id_val);
    picoquic_set64_connection_id(cnx_id_returned, val64);
}

int main(int argc, char** argv)
{
    const char* server_name = default_server_name;
    const char* server_cert_file = default_server_cert_file;
    const char* server_key_file = default_server_key_file;
    const char* log_file = NULL;
    const char* tls_secrets_file = NULL;
    const char * sni = NULL;
    const char * local_plugin_fnames[PICOQUIC_DEMO_MAX_PLUGIN_FILES];
    const char * both_plugin_fnames[PICOQUIC_DEMO_MAX_PLUGIN_FILES];
    int local_plugins = 0;
    int both_plugins = 0;
    int server_port = default_server_port;
    const char* root_trust_file = NULL;
    uint32_t proposed_version = 0xff00000b;
    int is_client = 0;
    int just_once = 0;
    int do_hrr = 0;
    int force_zero_share = 0;
    int cnx_id_mask_is_set = 0;
    int n_requests = 1;
    uint64_t requests_interval_microsec = 1;
    cnx_id_callback_ctx_t cnx_id_cbdata = {
        .cnx_id_select = 0,
        .cnx_id_mask = {{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 8 },
        .cnx_id_val = { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 0 }
    };
    uint64_t* reset_seed = NULL;
    uint64_t reset_seed_x[2];
    int mtu_max = 0;
    char *plugin_store_path = NULL;
    bool preload_plugins = false;

#ifdef _WINDOWS
    WSADATA wsaData;
#endif
    int ret = 0;

    /* HTTP09 test */
    int get_size = -1;
    int only_stream_4 = 0;

    char *qlog_filename = NULL;
    char *stats_filename = NULL;

    /* Get the parameters */
    int opt;
    while ((opt = getopt(argc, argv, "c:k:P:C:Q:G:p:v:L14rhzRX:S:i:s:l:m:n:t:q:w:N:I:W:F:")) != -1) {
        switch (opt) {
        case 'c':
            server_cert_file = optarg;
            break;
        case 'k':
            server_key_file = optarg;
            break;
        case 'P':
            local_plugin_fnames[local_plugins] = optarg;
            local_plugins++;
            break;
        case 'C':
            plugin_store_path = optarg;
            break;
        case 'Q':
            both_plugin_fnames[both_plugins] = optarg;
            both_plugins++;
            break;
        case 'G':
            if ((get_size = atoi(optarg)) <= 0) {
                fprintf(stderr, "Invalid GET size: %s\n", optarg);
                usage();
            }
            break;
        case '4':
            only_stream_4 = 1;
            break;
        case 'p':
            if ((server_port = atoi(optarg)) <= 0) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                usage();
            }
            break;
        case 'v':
            if (optind + 1 > argc) {
                fprintf(stderr, "option requires more arguments -- s\n");
                usage();
            }
            if ((proposed_version = parse_target_version(optarg)) == 0) {
                fprintf(stderr, "Invalid version: %s\n", optarg);
                usage();
            }
            break;
        case '1':
            just_once = 1;
            break;
        case 'L':
            preload_plugins = true;
            break;
        case 'r':
            do_hrr = 1;
            break;
        case 's':
            if (optind + 1 > argc) {
                fprintf(stderr, "option requires more arguments -- s\n");
                usage();
            }
            reset_seed = reset_seed_x; /* replacing the original alloca, which is not supported in Windows or BSD */
            reset_seed[1] = strtoul(argv[optind], NULL, 0);
            reset_seed[0] = strtoul(argv[optind++], NULL, 0);
            break;
        case 'i':
            if (optind + 2 > argc) {
                fprintf(stderr, "option requires more arguments -- i\n");
                usage();
            }

            cnx_id_cbdata.cnx_id_select = atoi(optarg);
            /* TODO: find an alternative to parsing a 64 bit integer */
            picoquic_set64_connection_id(&cnx_id_cbdata.cnx_id_mask, ~strtoul(argv[optind++], NULL, 0));
            picoquic_set64_connection_id(&cnx_id_cbdata.cnx_id_val, strtoul(argv[optind++], NULL, 0));
            cnx_id_mask_is_set = 1;
            break;
        case 'l':
            log_file = optarg;
            break;
        case 'X':
            tls_secrets_file = optarg;
            break;
        case 'm':
            mtu_max = atoi(optarg);
            if (mtu_max <= 0 || mtu_max > PICOQUIC_MAX_PACKET_SIZE) {
                fprintf(stderr, "Invalid max mtu: %s\n", optarg);
                usage();
            }
            break;
        case 'n':
            sni = optarg;
            break;
        case 't':
            root_trust_file = optarg;
            break;
        case 'z':
            force_zero_share = 1;
            break;
        case 'q':
            qlog_filename = optarg;
            break;
        case 'S':
            stats_filename = optarg;
            break;
        case 'R':
            ticket_store_filename = NULL;
            break;
        case 'w':
            max_stream_receive_window_size = atol(optarg);
            if (max_stream_receive_window_size <= 0) {
                fprintf(stderr, "Invalid max stream receive window size: %s\n", optarg);
                usage();
            }
            printf("SET RWIN TO %lu BYTES\n", max_stream_receive_window_size);
            break;
        case 'W':
            fixed_cwin = atol(optarg);
            break;
        case 'N':
            n_requests = atoi(optarg);
            if (n_requests <= 0) {
                fprintf(stderr, "Invalid n_requests: %s\n", optarg);
                usage();
            }
            break;
        case 'I':
            requests_interval_microsec = atol(optarg);
            break;
        case 'F':
            repair_receive_window_size = atol(optarg);
            printf("SET REPAIR RWIN %lu\n", repair_receive_window_size);
            break;
        case 'h':
            usage();
            break;
        }
    }

    /* Simplified style params */
    if (optind < argc) {
        server_name = argv[optind++];
        is_client = 1;
    }

    if (optind < argc) {
        if ((server_port = atoi(argv[optind++])) <= 0) {
            fprintf(stderr, "Invalid port: %s\n", optarg);
            usage();
        }
    }

#ifdef _WINDOWS
    // Init WSA.
    if (ret == 0) {
        if (WSA_START(MAKEWORD(2, 2), &wsaData)) {
            fprintf(stderr, "Cannot init WSA\n");
            ret = -1;
        }
    }
#endif

    FILE* F_log = NULL;

    if (log_file != NULL && strcmp(log_file, "/dev/null") != 0) {
#ifdef _WINDOWS
        if (fopen_s(&F_log, log_file, "w") != 0) {
                F_log = NULL;
            }
#else
        F_log = fopen(log_file, "w");
#endif
        if (F_log == NULL) {
            fprintf(stderr, "Could not open the log file <%s>\n", log_file);
        }
    }


    FILE* F_tls_secrets = NULL;

    if (tls_secrets_file != NULL && strcmp(tls_secrets_file, "/dev/null") != 0) {
#ifdef _WINDOWS
        if (fopen_s(&F_tls_secrets, tls_secrets_file, "w") != 0) {
                F_tls_secrets = NULL;
            }
#else
        F_tls_secrets = fopen(tls_secrets_file, "w");
#endif
        if (F_tls_secrets == NULL) {
            fprintf(stderr, "Could not open the log file <%s>\n", tls_secrets_file);
        }
    }


    if (!F_log && (!log_file || strcmp(log_file, "/dev/null") != 0)) {
        F_log = stdout;
    }

    if (is_client == 0) {
        if (plugin_store_path != NULL) {
            fprintf(stderr, "Do not support plugin cache at server side for now\n");
        }
        /* Run as server */
        printf("Starting PicoQUIC server on port %d, server name = %s, just_once = %d, hrr= %d, %d local plugins and %d both plugins\n",
            server_port, server_name, just_once, do_hrr, local_plugins, both_plugins);
        for(int i = 0; i < local_plugins; i++) {
            printf("\tlocal plugin %s\n", local_plugin_fnames[i]);
        }
        for(int i = 0; i < both_plugins; i++) {
            printf("\tlocal plugin %s\n", both_plugin_fnames[i]);
        }
        ret = quic_server(server_name, server_port,
            server_cert_file, server_key_file, just_once, do_hrr,
            /* TODO: find an alternative to using 64 bit mask. */
            (cnx_id_mask_is_set == 0) ? NULL : cnx_id_callback,
            (cnx_id_mask_is_set == 0) ? NULL : (void*)&cnx_id_cbdata,
            (uint8_t*)reset_seed, mtu_max, local_plugin_fnames, local_plugins,
            both_plugin_fnames, both_plugins, F_log, F_tls_secrets, qlog_filename, stats_filename, preload_plugins);
        printf("Server exit with code = %d\n", ret);
        if (F_tls_secrets != NULL && F_tls_secrets != stdout) {
            fclose(F_tls_secrets);
        }
    } else {
        if (F_log != NULL) {
            debug_printf_push_stream(F_log);
        }

        /* Run as client */
        printf("Starting PicoQUIC connection to server IP = %s, port = %d and %d local plugins\n", server_name, server_port, local_plugins);
        for(int i = 0; i < local_plugins; i++) {
            printf("\tlocal plugin %s\n", local_plugin_fnames[i]);
        }
        if (local_plugins > 0) {
            fprintf(stderr, "WARNING: direct plugin insertion at client might interfere with remote plugin injection...\n");
        }
        ret = quic_client(server_name, server_port, sni, root_trust_file, proposed_version, force_zero_share, mtu_max,
                          F_log, F_tls_secrets, local_plugin_fnames, local_plugins, get_size, only_stream_4, qlog_filename,
                          plugin_store_path, stats_filename, n_requests, requests_interval_microsec);

        printf("Client exit with code = %d\n", ret);

        if (F_log != NULL && F_log != stdout) {
            fclose(F_log);
        }
        if (F_tls_secrets != NULL && F_tls_secrets != stdout) {
            fclose(F_tls_secrets);
        }
    }
}
