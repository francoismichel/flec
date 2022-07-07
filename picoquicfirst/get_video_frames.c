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
#include <limits.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

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

static const int default_server_port = 4443;
static const char* default_server_name = "::";
static char* ticket_store_filename = "demo_ticket_store.bin";

static const char* bad_request_message = "<html><head><title>Bad Request</title></head><body>Bad request. Why don't you try \"GET /doc-456789.html\"?</body></html>";

#include "../picoquic/picoquic.h"
#include "../picoquic/picoquic_internal.h"
#include "../picoquic/picosocks.h"
#include "../picoquic/util.h"
#include "../picoquic/plugin.h"
#include "streamer.h"
#include "streamers_buffer.h"
#include "video_streamer.h"


static const char* response_buffer = NULL;
static size_t response_length = 0;

streamers_buffer_t streamers_buffer;
// the four values below can be overriden by the script arguments
size_t mean_message_size = 20000;
size_t message_size_std_dev = 10000;
uint64_t message_interval_microsec = 300000;
uint64_t streamer_seed_base = 12345;

uint8_t data_buffer[VIDEO_BUFFER_MAX_SIZE];

char *video_filename = NULL;

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
#define PICOQUIC_FIRST_RESPONSE_MAX (1 << 29)
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
    timed_video_streamer_t streamer;
    size_t command_length;
    size_t response_length;
    uint8_t command[PICOQUIC_FIRST_COMMAND_MAX];
} picoquic_first_server_stream_ctx_t;

typedef struct st_picoquic_first_server_callback_ctx_t {
    picoquic_first_server_stream_ctx_t* first_stream;
    size_t buffer_max;
    uint8_t* buffer;
} picoquic_first_server_callback_ctx_t;

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



int open_video_file(char *filename, AVFormatContext *pFormatContext, AVCodecContext **ppCodecContext, uint64_t *frame_interval_ns, int *video_stream_index) {
// AVFormatContext holds the header information from the format (Container)
    // Allocating memory for this component
    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
//    AVFormatContext *pFormatContext = avformat_alloc_context();
//    if (!pFormatContext) {
//        printf("ERROR could not allocate memory for Format Context");
//        return -1;
//    }
    *video_stream_index = -1;
    printf("opening the input file (%s) and loading format (container) header", filename);
    // Open the file and read its header. The codecs are not opened.
    // The function arguments are:
    // AVFormatContext (the component we allocated memory for),
    // url (filename),
    // AVInputFormat (if you pass NULL it'll do the auto detect)
    // and AVDictionary (which are options to the demuxer)
    // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga31d601155e9035d5b0e7efedc894ee49
    int err = 0;
    if ((err = avformat_open_input(&pFormatContext, filename, NULL, NULL)) != 0) {
        char buf[1000];
        av_strerror(err, buf, 1000);
        printf("ERROR could not open the file: %s\n", buf);
        return -1;
    }

    // now we have access to some information about our file
    // since we read its header we can say what format (container) it's
    // and some other information related to the format itself.
    printf("format %s, duration %ld us, bit_rate %ld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

    printf("finding stream info from format");
    // read Packets from the Format to get stream information
    // this function populates pFormatContext->streams
    // (of size equals to pFormatContext->nb_streams)
    // the arguments are:
    // the AVFormatContext
    // and options contains options for codec corresponding to i-th stream.
    // On return each dictionary will be filled with options that were not found.
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gad42172e27cddafb81096939783b157bb
    if (avformat_find_stream_info(pFormatContext,  NULL) < 0) {
        printf("ERROR could not get the stream info");
        return -1;
    }

    printf("NB STREAMD = %u\n", pFormatContext->nb_streams);

    // the component that knows how to enCOde and DECode the stream
    // it's the codec (audio or video)
    // http://ffmpeg.org/doxygen/trunk/structAVCodec.html
    AVCodec *pCodec = NULL;
    // this component describes the properties of a codec used by the stream i
    // https://ffmpeg.org/doxygen/trunk/structAVCodecParameters.html
    AVCodecParameters *pCodecParameters =  NULL;

    AVRational time_base_video;

    // loop though all the streams and print its main information
    for (int i = 0; i < pFormatContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters =  NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        printf("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        printf("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        printf("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
        printf("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);

        printf("finding the proper decoder (CODEC)");

        AVCodec *pLocalCodec = NULL;

        // finds the registered decoder for a codec ID
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec==NULL) {
            printf("ERROR unsupported codec!");
            return -1;
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (*video_stream_index == -1) {
                *video_stream_index = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
                time_base_video = pFormatContext->streams[i]->time_base;
                printf("AVG FRAME RATE = %d/%d\n", pFormatContext->streams[i]->avg_frame_rate.num, pFormatContext->streams[i]->avg_frame_rate.den);
//                printf("TIME BASE = %d/%d\n", time_base_video.num, time_base_video.den);
                *frame_interval_ns = 1000000*pFormatContext->streams[i]->avg_frame_rate.den/pFormatContext->streams[i]->avg_frame_rate.num;
            }

            printf("Video Codec: resolution %d x %d", pLocalCodecParameters->width, pLocalCodecParameters->height);
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Audio Codec: %d channels, sample rate %d", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
        }

        // print its name, id and bitrate
        printf("pLocalCodec = %p, pCodecParams = %p\n", pLocalCodec, pCodecParameters);
        printf("\tCodec %s ID %d bit_rate %ld", pLocalCodec->name, pLocalCodec->id, pCodecParameters->bit_rate);
    }
    // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext)
    {
        printf("failed to allocated memory for AVCodecContext");
        return -1;
    }

    // Fill the codec context based on the values from the supplied codec parameters
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#gac7b282f51540ca7a99416a3ba6ee0d16
    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
    {
        printf("failed to copy codec params to codec context");
        return -1;
    }

    // Initialize the AVCodecContext to use the given AVCodec.
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga11f785a188d7d9df71621001465b0f1d
    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0)
    {
        printf("failed to open codec through avcodec_open2");
        return -1;
    }
    *ppCodecContext = pCodecContext;
    return 0;
}


int release_video_resources(AVFormatContext *pFormatContext, AVCodecContext *pCodecContext) {
    avformat_close_input(&pFormatContext);
    avformat_free_context(pFormatContext);
    avcodec_free_context(&pCodecContext);
    return 0;
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
    stream_receiver_t stream_receiver;
    AVCodecContext *pCodecContext;
} picoquic_first_client_stream_ctx_t;

typedef struct st_picoquic_first_client_callback_ctx_t {
    demo_stream_desc_t const* demo_stream;
    size_t nb_demo_streams;

    struct st_picoquic_first_client_stream_ctx_t* first_stream;
    int nb_open_streams;
    uint32_t nb_client_streams;
    uint64_t last_interaction_time;
    int progress_observed;
    struct timeval tv_start;
    struct timeval tv_end;
    bool stream_ok;
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

        stream_receiver_init(&stream_ctx->stream_receiver);

        AVFormatContext *pFormatContext = avformat_alloc_context();
        if (!pFormatContext) {
            printf("could not allocate AVFormatContext\n");
            exit(-1);
        }
        uint64_t frame_interval_microsec = 0;
        int video_stream_idx = 0;
        // this is only used for getting the right coded parameters
        open_video_file(video_filename, pFormatContext, &stream_ctx->pCodecContext, &frame_interval_microsec, &video_stream_idx);
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

// h264 Codec ID = 27

static void first_client_callback(picoquic_cnx_t* cnx,
                                  uint64_t stream_id, uint8_t* bytes, size_t length,
                                  picoquic_call_back_event_t fin_or_event, void* callback_ctx)
{
    uint64_t fin_stream_id = 0xFFFFFFFF;

    picoquic_first_client_callback_ctx_t* ctx = (picoquic_first_client_callback_ctx_t*)callback_ctx;
    picoquic_first_client_stream_ctx_t* stream_ctx = ctx->first_stream;

    ctx->last_interaction_time = picoquic_current_time();
    ctx->progress_observed = 1;

    printf("EVENT::{\"time\": %ld, \"type\": \"stream_deliver\", \"range\": [%lu, %lu]}\n", picoquic_current_time(), stream_ctx->received_length, length);
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
            stream_receiver_receive_data(&stream_ctx->stream_receiver, bytes, length, 0);
            if (stream_ctx->stream_receiver.summary.contains_full_message) {
                ssize_t size = stream_receiver_get_message_payload(&stream_ctx->stream_receiver, data_buffer);
                printf("get full message of size %ld\n", size);
                AVPacket *packet = av_packet_alloc();
                av_packet_from_data(packet, data_buffer, size);

                AVFrame *pFrame = av_frame_alloc();
                if (!pFrame) {
                    printf("failed to allocated memory for AVFrame\n");
                    exit(-1);
                }
                char type = 0;
                printf("TRY TO DECODE\n");
                int err = decode_packet(packet, stream_ctx->pCodecContext, pFrame, &type);
                if (err) {
                    printf("error while decoding\n");
                    exit(-1);
                }
            }
            (void)fwrite(bytes, 1, length, stream_ctx->F);
            stream_ctx->received_length += length;
        }

        /* if FIN present, process request through http 0.9 */
        if (fin_or_event == picoquic_callback_stream_fin) {
            char buf[256];
            if (stream_id == 4) {
                gettimeofday(&ctx->tv_end, NULL);
                ctx->stream_ok = true;
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
                int get_size, int only_stream_4, char *qlog_filename, char *plugin_store_path, char *stats_filename)
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
        /* Create a client connection */
        cnx_client = picoquic_create_cnx(qclient, picoquic_null_connection_id, picoquic_null_connection_id,
                                         (struct sockaddr*)&server_address, current_time,
                                         proposed_version, sni, alpn, 1);

        if (cnx_client == NULL) {
            ret = -1;
        }
        else {
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

            picoquic_set_callback(cnx_client, first_client_callback, &callback_ctx);

            ret = picoquic_start_client_cnx(cnx_client);

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
        if (picoquic_is_cnx_backlog_empty(cnx_client) && callback_ctx.nb_open_streams == 0) {
            delay_max = 10000;
        } else {
            delay_max = 10000000;
        }

        from_length = to_length = sizeof(struct sockaddr_storage);

        bytes_recv = picoquic_select(&fd, 1, &packet_from, &from_length,
                                     &packet_to, &to_length, &if_index_to,
                                     buffer, sizeof(buffer),
                                     delta_t,
                                     &current_time,
                                     qclient);

        if (bytes_recv != 0) {
            if (F_log != NULL) {
                fprintf(F_log, "Select returns %d, from length %u\n", bytes_recv, from_length);
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
                                    tab->is_binary = 1;
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
                        }
                    }

                    client_ready_loop++;

                    if ((bytes_recv == 0 || client_ready_loop > 4) && picoquic_is_cnx_backlog_empty(cnx_client)) {
                        if (callback_ctx.nb_open_streams == 0) {
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

                delta_t = picoquic_get_next_wake_delay(qclient, picoquic_current_time(), delay_max);
            }
        }
    }

    /* Clean up */
    if (qclient != NULL) {
        uint8_t* ticket;
        uint16_t ticket_length;

        if (sni != NULL && 0 == picoquic_get_ticket(qclient->p_first_ticket, picoquic_current_time(), sni, (uint16_t)strlen(sni), alpn, (uint16_t)strlen(alpn), &ticket, &ticket_length)) {
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
        if (callback_ctx.stream_ok) {
            int time_us = (callback_ctx.tv_end.tv_sec - callback_ctx.tv_start.tv_sec) * 1000000 + callback_ctx.tv_end.tv_usec - callback_ctx.tv_start.tv_usec;
            printf("%d.%03d ms\n", time_us / 1000, time_us % 1000);
        } else {
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
    fprintf(stderr, "Usage: picoquicburst_messages [server_name [port]] <options>\n");
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
    fprintf(stderr, "  -i <base 10 long int> message sending interval (in microseconds)\n");
    fprintf(stderr, "  -d <base 10 long int> message length standard deviation in bytes\n");
    fprintf(stderr, "  -u <base 10 long int> message length mean in bytes\n");
    fprintf(stderr, "  -i message sending interval (in microseconds)\n");
    fprintf(stderr, "  -v version            Version proposed by client, e.g. -v ff00000a\n");
    fprintf(stderr, "  -z                    Set TLS zero share behavior on client, to force HRR.\n");
    fprintf(stderr, "  -l file               Log file\n");
    fprintf(stderr, "  -m mtu_max            Largest mtu value that can be tried for discovery\n");
    fprintf(stderr, "  -q output.qlog        qlog output file\n");
    fprintf(stderr, "  -S filename           if set, write plugin statistics in the specified file (- for stdout)\n");
    fprintf(stderr, "  -V filename           filename of the video to transmit)\n");
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

    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
        printf("ERROR could not allocate memory for Format Context");
        exit(-1);
    }
    if (argc < 2) {
        printf("ERROR we need the filename argument\n");
        exit(-1);
    }
    video_filename = argv[1];
    int video_filename_repr_len = strlen(video_filename) + strlen(".repr") + 1;
    char video_filename_repr[video_filename_repr_len];
    snprintf(video_filename_repr, video_filename_repr_len, "%s.repr", video_filename);
    printf("filename len = %d, filename = %s\n", video_filename_repr_len, video_filename_repr);
    FILE *video_repr = fopen(video_filename_repr, "w");
    uint64_t frame_interval_microsec = 0;
    int video_stream_idx;
    AVCodecContext *pCodecContext = NULL;   // unused
    open_video_file(video_filename, pFormatContext, &pCodecContext, &frame_interval_microsec, &video_stream_idx);
    timed_real_video_streamer_t tvs;
    int err = timed_real_video_streamer_init(&tvs, 4, pFormatContext, video_stream_idx, frame_interval_microsec, 1000000000);

    if (err) {
        printf("could not init the video streamer\n");
        exit(-1);
    }

    uint8_t *message_buffer;
    int64_t ct = timed_video_get_next_sending_timestamp(false, (abstract_video_streamer_t *) &tvs);
    int64_t initial_ct = ct;
    int64_t msg_len = -1;
    while(!timed_video_streamer_is_finished((timed_video_streamer_t *) &tvs)) {
        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        msg_len = timed_real_video_streamer_get_stream_bytes(&tvs, &message_buffer, ct, packet);

        // Supply raw packet data as input to a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
        int response = avcodec_send_packet(pCodecContext, packet);

        if (response < 0) {
            printf("Error while sending a packet to the decoder: %s", av_err2str(response));
            return response;
        }
        char type = '?';

        while (response >= 0) {
            // Return decoded output data (into a frame) from a decoder
            // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
            response = avcodec_receive_frame(pCodecContext, frame);
            if (response >= 0) {
                type = av_get_picture_type_char(frame->pict_type);
            }
        }
        av_packet_unref(packet);
        fprintf(video_repr, "%c%ld,%ld\n", type, ct - initial_ct, msg_len);
        ct = timed_video_get_next_sending_timestamp(false, (abstract_video_streamer_t *) &tvs);
        av_packet_unref(packet);
        av_frame_unref(frame);
    }
    fclose(video_repr);
}
