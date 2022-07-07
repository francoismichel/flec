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

//#define NS3

//#define DISABLE_FFMPEG (defined NS3 || defined ANDROID)
#ifndef DISABLE_FFMPEG
#if (defined NS3 || defined ANDROID || defined __ANDROID__)
#define DISABLE_FFMPEG
#endif
#endif

#ifndef DISABLE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#endif

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

static size_t max_stream_receive_window_size = SIZE_MAX;
static ssize_t initial_receive_window_size = 0;
static ssize_t repair_receive_window_size = -1L;

#include "../picoquic/picoquic.h"
#include "../picoquic/picosocks.h"
#include "../picoquic/util.h"
#include "../picoquic/plugin.h"
#include "streamer.h"
#include "streamers_buffer.h"
#include "video_streamer.h"

#define MIN(a, b) (((a) <= (b)) ? (a) : (b))

static const char* response_buffer = NULL;
static size_t response_length = 0;
static size_t handshake_response_length = 2;
bool post_request = false;

picoquic_congestion_algorithm_t* cc_algorithm = NULL;

streamers_buffer_t streamers_buffer;
// the four values below can be overriden by the script arguments
size_t mean_message_size = 20000;
size_t message_size_std_dev = 10000;
uint64_t message_interval_microsec = 300000;
uint64_t streamer_seed_base = 12345;
uint64_t server_keepalive_interval_microsec = 2000000;
uint64_t server_last_interaction_microsec = 0;
uint64_t first_fully_received_frame_timestamp = 0;

bool video_streamer_initialized = false;

uint64_t server_video_control_stream_id = 1;
uint64_t server_video_stats_feedback_stream_id = 5;
uint64_t handshake_stream_id = 0;

char *video_stats_filename = NULL;

uint8_t data_buffer[VIDEO_BUFFER_MAX_SIZE];


static protoop_id_t send_fec_protected_message = { .id = "fec_window_send_protected_message" };
static protoop_id_t send_fec_protected_unreliable_message = { .id = "fec_window_send_protected_unreliable_message" };
static protoop_id_t cancel_expired_fec_protected_unreliable_message = { .id = "fec_window_cancel_expired_protected_unreliable_messages" };
static protoop_id_t fec_next_message_microsec = { .id = "fec_window_next_message_microsec" };

static protoop_id_t congestion_set_fixed_cwin = { .id = "congestion_set_fixed_cwin" };

static protoop_id_t window_fec_framework_set_rwin_size = { .id = "window_fec_framework_set_rwin_size" };

/**
 * sets the  fixed cwin assuming the diable_congestion_control plugin is plugged
 */
int set_fixed_cwin(picoquic_cnx_t *cnx, uint64_t cwin) {
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &congestion_set_fixed_cwin, NULL, cwin);
    if (ret != 0) {
        fprintf(stdout, "could not send the protected message\n");
    }
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

bool use_pquic_fec_messages_api = false;
uint64_t default_frame_interval_microsec = 33333;
uint64_t messages_deadline_microsec = 250000;

const int64_t server_initiated_first_video_frame_stream_id = 3;
const int64_t client_initiated_first_video_frame_stream_id = 6;

int64_t server_current_frame_stream_id = server_initiated_first_video_frame_stream_id;
int64_t client_current_frame_stream_id = client_initiated_first_video_frame_stream_id;

int64_t control_stream_id = -1;

FILE *dev_null = NULL;

bool playback = false;
char *video_filename = NULL;

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

#define PICOQUIC_FIRST_COMMAND_MAX (1000000)
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
    abstract_video_streamer_t *streamer;
    stream_receiver_t *stream_receiver;
    receiver_summary_t receiver_summary;
#ifndef DISABLE_FFMPEG
    AVCodecContext *pCodecContext;
#endif
    size_t command_length;
    size_t response_length;
    uint8_t command[PICOQUIC_FIRST_COMMAND_MAX];
} picoquic_first_server_stream_ctx_t;

typedef struct st_picoquic_first_server_callback_ctx_t {
    picoquic_first_server_stream_ctx_t* first_stream;
    size_t buffer_max;
    uint8_t* buffer;
} picoquic_first_server_callback_ctx_t;


typedef struct st_picoquic_first_client_stream_ctx_t {
    struct st_picoquic_first_client_stream_ctx_t* next_stream;
    uint32_t stream_id;
    uint8_t command[PICOQUIC_FIRST_COMMAND_MAX + 1]; /* starts with "GET " */
    size_t received_length;
    FILE* F; /* NULL if stream is closed. */
    stream_receiver_t *stream_receiver;
    receiver_summary_t receiver_summary;
#ifndef DISABLE_FFMPEG
    AVCodecContext *pCodecContext;
#endif
} picoquic_first_client_stream_ctx_t;

typedef struct st_demo_stream_desc_t {
    uint32_t stream_id;
    uint32_t previous_stream_id;
    char const* doc_name;
    char const* f_name;
    int is_binary;
    size_t post_length;
    size_t padding_length;
    uint8_t* post_data; /* NULL if is_post is false. */
    bool is_post;
} demo_stream_desc_t;

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
    abstract_video_streamer_t *streamer;
    size_t response_length;
    FILE *video_stats_file;
    bool stream_ok;
    bool only_get;
} picoquic_first_client_callback_ctx_t;


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
    if (!filename) {
        out = NULL;
    } else if (strcmp(filename, "-")) {
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
            if (out) {
                fprintf(out, "%s\n", str);
            }
        }
    }
    free(stats);
    if (file) fclose(out);
}


#ifndef DISABLE_FFMPEG
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
#endif

// we know the type only for the playback streamer
size_t get_video_streamer_bytes(bool pb, void *streamer, uint8_t **buffer, uint64_t time, char *type) {
    *type = '?';
#ifndef DISABLE_FFMPEG
    if (pb) {
#endif
        return timed_playback_video_streamer_get_stream_bytes((timed_playback_video_streamer_t *) streamer, buffer, time, type);
#ifndef DISABLE_FFMPEG
    }
    AVPacket *packet = av_packet_alloc();
    size_t retval = timed_real_video_streamer_get_stream_bytes((timed_real_video_streamer_t *) streamer, buffer, time, packet);
    av_packet_unref(packet);
    return retval;
#endif
}

/**
 * int64_t next_message_microsec: the number of microsec before the next message sent by the application
 *                            -1 if you don't know
 */
int next_message_microsec(picoquic_cnx_t *cnx, int64_t next_message_microsec) {
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &fec_next_message_microsec, NULL, next_message_microsec);
    if (ret != 0) {
        fprintf(stdout, "could not send the protected message\n");
    }
    return ret;
}

/**
 * int64_t next_message_microsec: the number of microsec before the next message sent by the application
 *                            -1 if you don't know
 */
int cancel_expired_messages(picoquic_cnx_t *cnx) {
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &cancel_expired_fec_protected_unreliable_message, NULL, NULL);
    if (ret != 0) {
        fprintf(stdout, "could not send the protected message\n");
    }
    return ret;
}


/**
 * uint64_t stream_id the id of the stream on which to send the data,
   const uint8_t* data the data buffer to send
   size_t length the size of the data buffer to send
   int set_fin  set to true when it is the last message to send on this stream
   uint64_t data_lifetime the amount of time during which the data can be used if received by the peer (so it is useless to be transmitted if the deadline is bigger than the one-way delay)
                    set it to 0 if you want it to be protected by FEC as soon as possible. It won't be sent sooner but
                    the FEC will be sent sooner, making it potentially less efficient and waste a lot of bandwidth
                    compared to a correctly configured lifetime.
   currently can only handle streams until ID WINDOW_FEC_FRAMEWORK_MAX_HANDLED_STREAM_ID
 */
int old_send_protected_message(picoquic_cnx_t *cnx, uint64_t stream_id, const uint8_t* data, size_t length, int set_fin, uint64_t data_lifetime) {
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &send_fec_protected_message, NULL, stream_id, data, length, set_fin, data_lifetime);
    if (ret != 0) {
        fprintf(stdout, "could not send the protected message\n");
    }
    return ret;
}

/**
 * uint64_t stream_id the id of the stream on which to send the data: it MUST be unused yet !!
   const uint8_t* data the data buffer to send
   size_t length the size of the data buffer to send
   uint64_t data_lifetime the amount of time during which the data can be used if received by the peer (so it is useless to be transmitted if the deadline is bigger than the one-way delay)
                    set it to 0 if you want it to be protected by FEC as soon as possible. It won't be sent sooner but
                    the FEC will be sent sooner, making it potentially less efficient and waste a lot of bandwidth
                    compared to a correctly configured lifetime.
 */
int send_protected_message(picoquic_cnx_t *cnx, uint64_t stream_id, const uint8_t* data, size_t length, uint64_t data_lifetime) {
    int ret = protoop_prepare_and_run_extern_noparam(cnx, &send_fec_protected_unreliable_message, NULL, stream_id, data, length, data_lifetime);
    if (ret != 0) {
        fprintf(stdout, "could not send the protected message\n");
    }
    return ret;
}

static void write_video_stats_client(picoquic_cnx_t *cnx, char *filename, int64_t stream_id, picoquic_first_client_stream_ctx_t *stream_ctx) {
    char buf[200];
    FILE *video_stats_file = stdout;
    if (video_stats_filename) {
        video_stats_file = fopen(video_stats_filename, "w");
        if (!video_stats_file) {
            fprintf(stderr, "could not open video stats file: %s\n", strerror(errno));
            video_stats_file = stdout;
        }
    }
    fprintf(video_stats_file, "stream_id,size,frame_timestamp_ms,reception_timestamp_ms\n");
    while (stream_ctx) {
        if (stream_ctx->receiver_summary.contains_full_message) {
            snprintf(buf, 199, "%u,%ld,%ld,%ld\n", stream_ctx->stream_id,
                     stream_ctx->receiver_summary.message_size,
                     stream_ctx->receiver_summary.current_message_synchro_timestamp_ms,
                     stream_ctx->receiver_summary.current_message_reception_timestamp_us/1000);
            if (stream_id != -1L) {
                picoquic_add_to_stream(cnx, stream_id, (uint8_t *) buf, strlen(buf), 0);
            }
        }
        stream_ctx = stream_ctx->next_stream;
    }
    picoquic_add_to_stream(cnx, stream_id, (uint8_t *) buf, 0, 1);
}

static void write_video_stats_server(picoquic_cnx_t *cnx, char *filename, int64_t stream_id, picoquic_first_server_stream_ctx_t *stream_ctx) {
    char buf[200];
    FILE *video_stats_file = stdout;
    if (video_stats_filename) {
        video_stats_file = fopen(video_stats_filename, "w");
        if (!video_stats_file) {
            fprintf(stderr, "could not open video stats file: %s\n", strerror(errno));
            video_stats_file = stdout;
        }
    }
    char *header = "stream_id,size,frame_timestamp_ms,reception_timestamp_ms\n";
    fprintf(video_stats_file, header);
    if (stream_id != -1L) {
        picoquic_add_to_stream(cnx, stream_id, (uint8_t *) header, strlen(header), stream_ctx->next_stream == NULL);
    }
    while (stream_ctx) {
        if (stream_ctx->receiver_summary.contains_full_message) {
            snprintf(buf, 199, "%lu,%ld,%ld,%ld\n", stream_ctx->stream_id,
                     stream_ctx->receiver_summary.message_size,
                     stream_ctx->receiver_summary.current_message_synchro_timestamp_ms,
                     stream_ctx->receiver_summary.current_message_reception_timestamp_us/1000);
            if (stream_id != -1L) {
                picoquic_add_to_stream(cnx, stream_id, (uint8_t *) buf, strlen(buf), 0);
            }
        }
        stream_ctx = stream_ctx->next_stream;
    }
    picoquic_add_to_stream(cnx, stream_id, (uint8_t *) buf, 0, 1);
}

abstract_video_streamer_t *init_video_streamer(picoquic_cnx_t *cnx, int64_t stream_id, uint64_t time, uint64_t response_length, int server) {


    uint64_t frame_interval_microsec = 0;
    abstract_video_streamer_t *streamer = malloc(sizeof(abstract_video_streamer_t));
    if (!streamer) {
        printf("out of memory !\n");
        exit(-1);
    }
    memset(streamer, 0, sizeof(abstract_video_streamer_t));
#ifndef DISABLE_FFMPEG
    if (!playback) {
                        AVFormatContext *pFormatContext = avformat_alloc_context();
                        if (!pFormatContext) {
                            printf("ERROR could not allocate memory for Format Context");
                            exit(-1);
                        }
                        int video_stream_idx;
                        AVCodecContext *pCodecContext = NULL;   // unused
                        open_video_file(video_filename, pFormatContext, &pCodecContext, &frame_interval_microsec, &video_stream_idx);

                        int err = timed_real_video_streamer_init(streamer, stream_id, pFormatContext, video_stream_idx, frame_interval_microsec, response_length);

                        if (err) {
                            printf("could not init the video streamer\n");
                            exit(-1);
                        }
                    } else {
#endif
    int err = timed_playback_video_streamer_init(streamer, stream_id, video_filename, response_length);
    if (err) {
        printf("could not init the video streamer\n");
        exit(-1);
    }
    default_frame_interval_microsec = ((timed_playback_video_streamer_t *) streamer)->streamer.interval_microsec;
    printf("frames interval = %lu microsec\n", default_frame_interval_microsec);
#ifndef  DISABLE_FFMPEG
    }
#endif

    streamers_buffer_add_streamer(&streamers_buffer, streamer, cnx);
    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
    uint8_t *message_buffer = NULL;
    char frame_type = '?';
    size_t nbytes = get_video_streamer_bytes(playback, streamer, &message_buffer, time, &frame_type);
    printf("init, nbytes = %lu\n", nbytes);
    control_stream_id = stream_id;
    if (nbytes != 0) {
        int64_t id = 0;
        int64_t *current_frame_id = server ? (&server_current_frame_stream_id) : (&client_current_frame_stream_id);
        if (true || use_pquic_fec_messages_api) {
            id = *current_frame_id;
            if (use_pquic_fec_messages_api) {

                send_protected_message(cnx, *current_frame_id, message_buffer,
                                       nbytes, messages_deadline_microsec);
                next_message_microsec(cnx, default_frame_interval_microsec);
            } else {
                picoquic_add_to_stream(cnx, *current_frame_id, message_buffer,
                                       nbytes, 1);
            }
            *current_frame_id += 4;
            if (timed_video_streamer_is_finished(streamer)) {
                printf("send END OF VIDEO at the beginning !\n");
                picoquic_add_to_stream(cnx, control_stream_id, "end of video", strlen("end of video"), true);
            }
            if (use_pquic_fec_messages_api) {
                cancel_expired_messages(cnx);
            }
        } else {
            id = stream_id;
        }
        printf("EVENT::{\"time\": %ld, \"type\": \"message_enqueue\", \"length\": %lu, \"stream_id\": %ld}\n", picoquic_current_time(), nbytes, id);
    }
    return streamer;
}

static int first_server_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx)
{
    picoquic_first_server_callback_ctx_t* ctx = (picoquic_first_server_callback_ctx_t*)callback_ctx;
    picoquic_first_server_stream_ctx_t* stream_ctx = NULL;

    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
    uint64_t time = picoquic_current_time();
    picoquic_log_time(stdout, cnx, time, "", " : ");
    printf("Server CB, Stream: %" PRIu64 ", %" PRIst " bytes, fin=%d (%s)\n",
        stream_id, length, fin_or_event, picoquic_log_fin_or_event_name(fin_or_event));


    if (fin_or_event == picoquic_callback_prepare_to_send) {
        /* Unexpected call. */
        return -1;
    }

    if (fin_or_event == picoquic_callback_almost_ready ||
        fin_or_event == picoquic_callback_ready ||
        fin_or_event == picoquic_callback_challenge_response) {
        /* Nothing to do */
        return 0;
    }

    if (fin_or_event == picoquic_callback_close || 
        fin_or_event == picoquic_callback_application_close ||
        fin_or_event == picoquic_callback_stateless_reset) {
        if (ctx != NULL) {
            first_server_callback_delete_context(ctx);
            picoquic_set_callback(cnx, first_server_callback, NULL);
        }
        fflush(stdout);
        return 0;
    }

    if (fin_or_event == picoquic_callback_challenge_response) {
        fflush(stdout);
        return 0;
    }

    if (ctx == NULL) {
        picoquic_first_server_callback_ctx_t* new_ctx = first_server_callback_create_context();
        if (new_ctx == NULL) {
            /* cannot handle the connection */
            printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
            printf("Memory error, cannot allocate application context\n");

            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return 0;
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
            return 0;
        } else {
            memset(stream_ctx, 0, sizeof(picoquic_first_server_stream_ctx_t));
            stream_ctx->next_stream = ctx->first_stream;
            ctx->first_stream = stream_ctx;
            stream_ctx->stream_id = stream_id;
        }
    }


    uint64_t current_time = picoquic_current_time();
    /* verify state and copy data to the stream buffer */
    if (fin_or_event == picoquic_callback_stop_sending) {
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, 0);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Stop Sending Stream: %" PRIu64 ", resetting the local stream.\n",
            stream_id);
        return 0;
    } else if (fin_or_event == picoquic_callback_stream_reset) {
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, 0);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Reset Stream: %" PRIu64 ", resetting the local stream.\n",
            stream_id);
        return 0;
    } else if (stream_ctx->status == picoquic_first_server_stream_status_finished || stream_ctx->command_length + length > (PICOQUIC_FIRST_COMMAND_MAX - 1)) {
        if (fin_or_event == picoquic_callback_stream_fin && length == 0) {
            /* no problem, this is fine. */
        } else {
            /* send after fin, or too many bytes => reset! */
            picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_STREAM_STATE_ERROR);
            printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
            printf("Server CB, Stream: %" PRIu64 ", RESET, too long or after FIN, command length = %ld, status = %d\n",
                stream_id, stream_ctx->command_length, stream_ctx->status);
        }
        return 0;
    } else if (fin_or_event == picoquic_callback_stream_gap) {
        /* We do not support this, yet */
        stream_ctx->status = picoquic_first_server_stream_status_finished;
        picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_PROTOCOL_VIOLATION);
        printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
        printf("Server CB, Stream: %" PRIu64 ", RESET, stream gaps not supported\n", stream_id);
        return 0;
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

            if (current_time > server_last_interaction_microsec + server_keepalive_interval_microsec) {
                if (stream_id > server_video_control_stream_id) {  // small hack to avoid sending data on stream 4 too early (before it was open)
                    picoquic_add_to_stream(cnx, server_video_control_stream_id, (uint8_t *) "keep-alive",
                                           strlen("keep-alive"), 0);
                    server_last_interaction_microsec = current_time;
                }
            }

            if (post_request) {
                if (!stream_ctx->stream_receiver) {
                    stream_ctx->stream_receiver = malloc(sizeof(stream_receiver_t));
                    if (!stream_ctx->stream_receiver) {
                        fprintf(stdout, "cannot allocate stream receiver\n");
                        return -1;
                    }
                    stream_receiver_init(stream_ctx->stream_receiver);
                }
                stream_receiver_receive_data(stream_ctx->stream_receiver, bytes, length, current_time);
                if (stream_ctx->stream_receiver->summary.contains_full_message) {
                    if (first_fully_received_frame_timestamp == 0) {
                        first_fully_received_frame_timestamp = current_time;
                    }
                    ssize_t size = stream_receiver_get_message_payload(stream_ctx->stream_receiver,
                                                                       data_buffer);
                    printf("get full message of size %ld, sync = %lu, now = %lu, now relative in ms = %lu\n", size,
                           stream_ctx->stream_receiver->summary.current_message_synchro_timestamp_ms,
                           stream_ctx->stream_receiver->summary.current_message_reception_timestamp_us, (current_time - first_fully_received_frame_timestamp) / 1000);
                    stream_ctx->receiver_summary = stream_ctx->stream_receiver->summary;

#ifndef DISABLE_FFMPEG
                    if (!playback) {
                        AVPacket *packet = av_packet_alloc();
                        av_packet_from_data(packet, data_buffer, size);

                        AVFrame *pFrame = av_frame_alloc();
                        if (!pFrame) {
                            printf("failed to allocated memory for AVFrame\n");
                            exit(-1);
                        }
                        char type = 0;
                        int err = decode_packet(packet, stream_ctx->pCodecContext, pFrame, &type);
                        if (err) {
                            printf("error while decoding\n");
                            exit(-1);
                        }
                    }
#endif

                }
            } else {
                // video download
            }
        }

        /* if FIN present, process request through http 0.9 */
        if ((fin_or_event == picoquic_callback_stream_fin || (crlf_present != 0 && !post_request)) && stream_ctx->response_length == 0) {
            stream_ctx->command[stream_ctx->command_length] = 0;
            /* if data generated, just send it. Otherwise, just FIN the stream. */
            stream_ctx->status = picoquic_first_server_stream_status_finished;

            if (response_length > 0) {
                printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
                printf("Server CB, Stream: %" PRIu64 ", Sending %lu-byte static response\n",
                       stream_id, response_length);
                stream_ctx->response_length = response_length;
                picoquic_add_to_stream(cnx, stream_ctx->stream_id, (const uint8_t*) response_buffer, response_length, timed_video_streamer_is_finished(stream_ctx->streamer));
            } else {
                char buf[256];
                if (strncmp((char *) stream_ctx->command, "GET /video-upload", MIN(strlen("video-upload"), stream_ctx->command_length)) == 0) {
                    // video upload from the client
                    post_request = true;
                } else if (strncmp((char *) stream_ctx->command, "end of video", MIN(strlen("end of video"), stream_ctx->command_length)) == 0) {
                    fprintf(stdout, "send end to the client\n");
                    picoquic_add_to_stream(cnx, stream_id, (const uint8_t*) "end", strlen("end"), 1);
                    picoquic_add_to_stream(cnx, server_video_control_stream_id, (const uint8_t*) "end", strlen("end"), 1);
                    write_video_stats_server(cnx, NULL, (int64_t) server_video_stats_feedback_stream_id, ctx->first_stream);
                } else if (!post_request && http0dot9_get(stream_ctx->command, stream_ctx->command_length,
                                  ctx->buffer, ctx->buffer_max, &stream_ctx->response_length) != 0) {
                    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
                    printf("Server CB, Stream: %" PRIu64 ", Reply with bad request message after command: %s\n",
                           stream_id, strip_endofline(buf, sizeof(buf), (char *) &stream_ctx->command));

                    // picoquic_reset_stream(cnx, stream_id, 404);

                    (void) picoquic_add_to_stream(cnx, stream_ctx->stream_id, (const uint8_t *) bad_request_message,
                                                  strlen(bad_request_message), 1);
                } else {
                    if (stream_id == 4) {
                        stream_ctx->streamer = init_video_streamer(cnx, stream_id, time, stream_ctx->response_length,
                                                                   1);
                        video_streamer_initialized = true;
                    } else if (stream_id == handshake_stream_id) {
                        picoquic_add_to_stream(cnx, handshake_stream_id, (uint8_t *)data_buffer, handshake_response_length, true);
                    }

                    // below is the same content as init_video_streamer()
//                    uint64_t frame_interval_microsec = 0;
//                    stream_ctx->streamer = malloc(sizeof(abstract_video_streamer_t));
//                    if (!stream_ctx->streamer) {
//                        printf("out of memory !\n");
//                        exit(-1);
//                    }
//                    memset(stream_ctx->streamer, 0, sizeof(abstract_video_streamer_t));
//#ifndef DISABLE_FFMPEG
//                    if (!playback) {
//                        AVFormatContext *pFormatContext = avformat_alloc_context();
//                        if (!pFormatContext) {
//                            printf("ERROR could not allocate memory for Format Context");
//                            exit(-1);
//                        }
//                        int video_stream_idx;
//                        AVCodecContext *pCodecContext = NULL;   // unused
//                        open_video_file(video_filename, pFormatContext, &pCodecContext, &frame_interval_microsec, &video_stream_idx);
//
//                        int err = timed_real_video_streamer_init(stream_ctx->streamer, stream_id, pFormatContext, video_stream_idx, frame_interval_microsec, stream_ctx->response_length);
//
//                        if (err) {
//                            printf("could not init the video streamer\n");
//                            exit(-1);
//                        }
//                    } else {
//#endif
//                        printf("init playback\n");
//                        int err = timed_playback_video_streamer_init(stream_ctx->streamer, stream_id, video_filename, stream_ctx->response_length);
//                        if (err) {
//                            printf("could not init the video streamer\n");
//                            exit(-1);
//                        }
//                        default_frame_interval_microsec = ((timed_playback_video_streamer_t *) stream_ctx->streamer)->streamer.interval_microsec;
//                        printf("frames interval = %lu microsec\n", default_frame_interval_microsec);
//#ifndef  DISABLE_FFMPEG
//                    }
//#endif
//
//                    streamers_buffer_add_streamer(&streamers_buffer, stream_ctx->streamer, cnx);
//                    printf("%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx)));
//                    printf("Server CB, Stream: %" PRIu64 ", Processing command: %s\n",
//                           stream_id, strip_endofline(buf, sizeof(buf), (char *) &stream_ctx->command));
//                    uint8_t *message_buffer = NULL;
//                    char frame_type = '?';
//                    size_t nbytes = get_video_streamer_bytes(playback, stream_ctx->streamer, &message_buffer, time, &frame_type);
//                    printf("init, nbytes = %lu\n", nbytes);
//                    if (nbytes != 0) {
//                        int64_t id = 0;
//
////                        if (use_pquic_fec_messages_api) {
//////                            send_protected_message(cnx, stream_id, message_buffer,
//////                                                   nbytes, timed_video_streamer_is_finished(stream_ctx->streamer), messages_deadline_microsec);
////                            id = server_current_frame_stream_id;
////                            send_protected_message(cnx, server_current_frame_stream_id, message_buffer,
////                                                   nbytes, messages_deadline_microsec);
////                            server_current_frame_stream_id += 4;
////                            control_stream_id = stream_id;
////                            next_message_microsec(cnx, default_frame_interval_microsec);
////                            if (timed_video_streamer_is_finished(stream_ctx->streamer)) {
////                                printf("send END OF VIDEO at the beginning !\n");
////                                picoquic_add_to_stream(cnx, control_stream_id, "end of video", strlen("end of video"), true);
////                            }
////                            cancel_expired_messages(cnx);
////                        } else {
////                            id = stream_id;
////                            picoquic_add_to_stream(cnx, stream_id, message_buffer,
////                                                   nbytes, timed_video_streamer_is_finished(stream_ctx->streamer));
////                        }
//
//                        if (true || use_pquic_fec_messages_api) {
////                            send_protected_message(cnx, stream_id, message_buffer,
////                                                   nbytes, timed_video_streamer_is_finished(stream_ctx->streamer), messages_deadline_microsec);
//                            id = server_current_frame_stream_id;
//                            if (use_pquic_fec_messages_api) {
//                                send_protected_message(cnx, server_current_frame_stream_id, message_buffer,
//                                                       nbytes, messages_deadline_microsec);
//                                next_message_microsec(cnx, default_frame_interval_microsec);
//                            } else {
//                                picoquic_add_to_stream(cnx, server_current_frame_stream_id, message_buffer,
//                                                       nbytes, 1);
//                            }
//                            server_current_frame_stream_id += 4;
//                            control_stream_id = stream_id;
//                            if (timed_video_streamer_is_finished(stream_ctx->streamer)) {
//                                printf("send END OF VIDEO at the beginning !\n");
//                                picoquic_add_to_stream(cnx, control_stream_id, "end of video", strlen("end of video"), true);
//                            }
//                            if (use_pquic_fec_messages_api) {
//                                cancel_expired_messages(cnx);
//                            }
//                        } else {
//                            id = stream_id;
//                        }
//                        printf("EVENT::{\"time\": %ld, \"type\": \"message_enqueue\", \"length\": %lu, \"stream_id\": %ld}\n", picoquic_current_time(), nbytes, id);
//                    }
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
        printf("Server CB, Stream: %" PRIu64 ", unexpected event %d\n", stream_id, fin_or_event);
        return 0;
    }

    return 0;
    /* that's it */
}

#ifndef DISABLE_FFMPEG
int release_video_resources(AVFormatContext *pFormatContext, AVCodecContext *pCodecContext) {
    avformat_close_input(&pFormatContext);
    avformat_free_context(pFormatContext);
    avcodec_free_context(&pCodecContext);
    return 0;
}
#endif

int enqueue_streamers_messages(int n_streamers, streamers_buffer_slot_t *streamers, uint64_t current_time, int server) {

    // check if there are messages to enqueue in the streams
    // TODO: it could be done more efficiently by just looking at the smallest_timestamp information
    for (int i = 0 ; i < n_streamers ; i++) {
        timed_video_streamer_t *streamer = (timed_video_streamer_t *) streamers[i].streamer;
        if (timed_video_streamer_should_send_now(playback, streamer, current_time)) {
            uint8_t *message = NULL;
            char frame_type = '?';
            size_t msg_size = get_video_streamer_bytes(playback, streamer, &message, current_time, &frame_type);
            if (msg_size != 0) {

                int64_t *current_frame_stream_id = server ? (&server_current_frame_stream_id) : (&client_current_frame_stream_id);

                int64_t stream_id = 0;
                if (true || use_pquic_fec_messages_api) {
                    stream_id = *current_frame_stream_id;
                    *current_frame_stream_id += 4;
                    if (use_pquic_fec_messages_api) {
                        send_protected_message((picoquic_cnx_t *) streamers[i].streamer_ctx, stream_id, message,
                                               msg_size, messages_deadline_microsec);

                        uint64_t next_ts = timed_video_streamer_next_timestamp(playback, streamer);
                        printf("SEND DEADLINE %lu, NEXT MESSAGE MICROSEC %lu\n", messages_deadline_microsec, next_ts > current_time ? (next_ts - current_time) : 0);
                        next_message_microsec((picoquic_cnx_t *) streamers[i].streamer_ctx, next_ts > current_time ? (next_ts - current_time) : 0);
//                        next_message_microsec((picoquic_cnx_t *) streamers[i].streamer_ctx, default_frame_interval_microsec);
                    } else {
                        picoquic_add_to_stream((picoquic_cnx_t *) streamers[i].streamer_ctx, stream_id,
                                               message, msg_size, true);
                    }
                    if (timed_video_streamer_is_finished(streamer)) {
                        uint64_t stream_id_to_send_end_of_connection = server ? control_stream_id : (control_stream_id + 4);
                        printf("SEND END OF VIDEO to stream %ld\n", stream_id_to_send_end_of_connection);
                        picoquic_add_to_stream((picoquic_cnx_t *) streamers[i].streamer_ctx, stream_id_to_send_end_of_connection, (uint8_t *) "end of video", strlen("end of video"), true);
                    }
                    if (use_pquic_fec_messages_api) {

                        cancel_expired_messages((picoquic_cnx_t *) streamers[i].streamer_ctx);
                    }
                } else {
                    stream_id = streamer->simple_streamer.stream_id;
                    picoquic_add_to_stream((picoquic_cnx_t *) streamers[i].streamer_ctx, streamer->simple_streamer.stream_id,
                                           message, msg_size, timed_video_streamer_is_finished(streamer));
                }
                printf("queue %lu bytes for stream %lu\n", msg_size, stream_id);
                printf("EVENT::{\"time\": %ld, \"type\": \"message_enqueue\", \"length\": %lu, \"stream_id\": %lu}\n", picoquic_current_time(), msg_size, stream_id);
            }
        }
    }
    return 0;

}

#define PICOQUIC_DEMO_SERVER_MAX_SEND_BATCH 5
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

    streamers_buffer_slot_t current_streamers[MAX_STREAMERS_BUFFER_SIZE];

#ifdef STATIC_RESPONSE
    response_buffer = (const char *) malloc(STATIC_RESPONSE);
    if (!response_buffer) {
        fprintf(stderr, "Unable to allocated %d-byte static response\n", STATIC_RESPONSE);
        exit(-1);
    }
    memset((char *) response_buffer, 42, STATIC_RESPONSE);
    response_length  = STATIC_RESPONSE;
#endif

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

    picoquic_tp_t tp;
    memset(&tp, 0, sizeof(picoquic_tp_t));
    if (ret == 0) {

        picoquic_init_transport_parameters(&tp, 0);
        // ensure that the receive window is respected at the beginning of the transfer
        tp.initial_max_stream_data_bidi_local = MIN(
                MAX(initial_receive_window_size, tp.initial_max_stream_data_bidi_local),
                max_stream_receive_window_size);
        tp.initial_max_stream_data_bidi_remote = MIN(
                MAX(initial_receive_window_size, tp.initial_max_stream_data_bidi_remote),
                max_stream_receive_window_size);
        tp.initial_max_stream_data_uni = MIN(MAX(initial_receive_window_size, tp.initial_max_stream_data_uni),
                                             max_stream_receive_window_size);
        tp.initial_max_data = MAX(initial_receive_window_size, tp.initial_max_data);
        qserver->default_tp = &tp;
    }

    /* Wait for packets */
    while (ret == 0 && (just_once == 0 || cnx_server == NULL || picoquic_get_cnx_state(cnx_server) != picoquic_state_disconnected)) {
        uint64_t time_before = picoquic_current_time();
        uint64_t current_time = picoquic_current_time();
        int64_t delta_t = picoquic_get_next_wake_delay(qserver, picoquic_current_time(), delay_max);

        int has_active_streamer = 0;

        int n_streamers = get_all_streamers(&streamers_buffer, current_streamers);

        uint64_t smallest_timestamp = ~((uint64_t) 0);

        printf("n_streamers = %d\n", n_streamers);

        for (int i = 0 ; i < n_streamers ; i++) {
            timed_video_streamer_t *streamer = (timed_video_streamer_t *) current_streamers[i].streamer;
            if (!timed_video_streamer_is_finished(streamer)) {
                has_active_streamer = 1;
                uint64_t next_sending_timestamp = timed_video_get_next_sending_timestamp(playback, streamer);
                if (smallest_timestamp > next_sending_timestamp) {
                    smallest_timestamp = next_sending_timestamp;
                }
            }
        }

        if (smallest_timestamp > current_time) {
            delta_t = (delta_t < (smallest_timestamp - current_time)) ? delta_t : (smallest_timestamp - current_time);
        } else if (has_active_streamer) {
            delta_t = 0;
        }

        printf("delta_t = %lu\n", delta_t);

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

        // check if there are messages to enqueue in the streams
        enqueue_streamers_messages(n_streamers, current_streamers, current_time, 1);
        // TODO: it could be done more efficiently by just looking at the smallest_timestamp information
//        for (int i = 0 ; i < n_streamers ; i++) {
//            timed_video_streamer_t *streamer = (timed_video_streamer_t *) current_streamers[i].streamer;
//            if (timed_video_streamer_should_send_now(playback, streamer, current_time)) {
//                uint8_t *message = NULL;
//                char frame_type = '?';
//                size_t msg_size = get_video_streamer_bytes(playback, streamer, &message, current_time, &frame_type);
//                printf("frame type = %c\n", frame_type);
//                if (msg_size != 0) {
//
//                    int64_t stream_id = 0;
//                    if (true || use_pquic_fec_messages_api) {
//                        stream_id = server_current_frame_stream_id;
//                        server_current_frame_stream_id += 4;
//                        if (use_pquic_fec_messages_api) {
//                            send_protected_message((picoquic_cnx_t *) current_streamers[i].streamer_ctx, stream_id, message,
//                                                   msg_size, messages_deadline_microsec);
//                            next_message_microsec((picoquic_cnx_t *) current_streamers[i].streamer_ctx, default_frame_interval_microsec);
//                        } else {
//                            picoquic_add_to_stream((picoquic_cnx_t *) current_streamers[i].streamer_ctx, stream_id,
//                                                   message, msg_size, true);
//                        }
//                        if (timed_video_streamer_is_finished(streamer)) {
//                            printf("SEND END OF VIDEO\n");
//                            picoquic_add_to_stream((picoquic_cnx_t *) current_streamers[i].streamer_ctx, control_stream_id, (uint8_t *) "end of video", strlen("end of video"), true);
//                        }
//                        if (use_pquic_fec_messages_api) {
//
//                            cancel_expired_messages((picoquic_cnx_t *) current_streamers[i].streamer_ctx);
//                        }
//                    } else {
//                        stream_id = streamer->simple_streamer.stream_id;
//                        picoquic_add_to_stream((picoquic_cnx_t *) current_streamers[i].streamer_ctx, streamer->simple_streamer.stream_id,
//                                               message, msg_size, timed_video_streamer_is_finished(streamer));
//                    }
//                    printf("queue %lu bytes for stream %lu\n", msg_size, stream_id);
//                    printf("EVENT::{\"time\": %ld, \"type\": \"message_enqueue\", \"length\": %lu, \"stream_id\": %lu}\n", picoquic_current_time(), msg_size, stream_id);
//                }
//            }
//        }

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

                    if (cc_algorithm) {
                        picoquic_set_congestion_algorithm(cnx_server, cc_algorithm);
                    }

                    if (repair_receive_window_size != -1L) {
                        if (max_stream_receive_window_size == -1L) {
                            max_stream_receive_window_size = 2000000;
                        }
                        ret = fec_set_rwin_size(cnx_server, max_stream_receive_window_size, repair_receive_window_size);
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

                if (cnx_server && fixed_cwin != 0) {
                    printf("SET FIXED CWIN TO %lu (cnx = %p)\n", fixed_cwin, cnx_server);
                    set_fixed_cwin(cnx_server, fixed_cwin);
                }

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

                int n_loop_turns = 0;
                while (ret == 0 && bytes_recv == 0 && n_loop_turns++ < PICOQUIC_DEMO_SERVER_MAX_SEND_BATCH && (cnx_next = picoquic_get_earliest_cnx_to_wake(qserver, loop_time)) != NULL) {
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
                                cnx_next->cnx_state >= picoquic_state_disconnecting ) {
                                fprintf(stdout,"%" PRIx64 ": ", picoquic_val64_connection_id(picoquic_get_logging_cnxid(cnx_next)));
                                fprintf(stdout,"Connection state = %d\n",
                                    picoquic_get_cnx_state(cnx_next));
                            }

                            picoquic_get_peer_addr(path, &peer_addr, &peer_addr_len);
                            picoquic_get_local_addr(path, &local_addr, &local_addr_len);

                            /* QDC: I hate having those lines here... But it is the only place to hook before sending... */
                            /* Both Linux and Windows use separate sockets for V4 and V6 */
#ifndef DISABLE_FFMPEG
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
        stream_ctx->stream_receiver = malloc(sizeof(stream_receiver_t));
        if (!stream_ctx->stream_receiver) {
            ret = -1;
            fprintf(stdout, "cannot allocate stream receiver\n");
        }
        stream_receiver_init(stream_ctx->stream_receiver);

        uint64_t frame_interval_microsec = 0;
#ifndef DISABLE_FFMPEG
        if (!playback) {
            AVFormatContext *pFormatContext = avformat_alloc_context();
            if (!pFormatContext) {
                printf("could not allocate AVFormatContext\n");
                exit(-1);
            }
            int video_stream_idx = 0;
            // this is only used for getting the right coded parameters
            open_video_file(video_filename, pFormatContext, &stream_ctx->pCodecContext, &frame_interval_microsec, &video_stream_idx);
        }
#endif
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
static picoquic_first_client_stream_ctx_t *demo_client_open_receive_stream(picoquic_cnx_t* cnx,
    picoquic_first_client_callback_ctx_t* ctx,
    uint32_t stream_id)
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
        printf("Opening receive-stream %u\n", stream_id);

        memset(stream_ctx, 0, sizeof(picoquic_first_client_stream_ctx_t));
        stream_ctx->stream_id = stream_id;

        stream_ctx->next_stream = ctx->first_stream;
        ctx->first_stream = stream_ctx;
        stream_ctx->stream_receiver = malloc(sizeof(stream_receiver_t));
        if (!stream_ctx->stream_receiver) {
            ret = -1;
            fprintf(stdout, "cannot allocate stream receiver\n");
            return NULL;
        }
        stream_receiver_init(stream_ctx->stream_receiver);
        uint64_t frame_interval_microsec = 0;
#ifndef DISABLE_FFMPEG
        if (!playback) {
            AVFormatContext *pFormatContext = avformat_alloc_context();
            if (!pFormatContext) {
                printf("could not allocate AVFormatContext\n");
                exit(-1);
            }
            int video_stream_idx = 0;
            // this is only used for getting the right coded parameters
            open_video_file(video_filename, pFormatContext, &stream_ctx->pCodecContext, &frame_interval_microsec, &video_stream_idx);
        }
#endif
#ifdef _WINDOWS
        if (fopen_s(&stream_ctx->F, fname, (is_binary == 0) ? "w" : "wb") != 0) {
            ret = -1;
        }
#else

        stream_ctx->F = dev_null;
        if (stream_ctx->F == NULL) {
            ret = -1;
        }
#endif
        if (ret != 0) {
            fprintf(stdout, "Cannot use /dev/null for video frames !\n");
        } else {
            ctx->nb_open_streams++;
            ctx->nb_client_streams++;
        }
    }
    return stream_ctx;
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

static int first_client_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx)
{
    uint64_t fin_stream_id = 0xFFFFFFFF;

    picoquic_first_client_callback_ctx_t* ctx = (picoquic_first_client_callback_ctx_t*)callback_ctx;
    picoquic_first_client_stream_ctx_t* stream_ctx = ctx->first_stream;

    uint64_t current_time = picoquic_current_time();

    ctx->last_interaction_time = current_time;
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
                if (stream_ctx->F != dev_null) {
                    fclose(stream_ctx->F);
                }
                stream_ctx->F = NULL;
                ctx->nb_open_streams--;

                printf("On stream %u, command: %s stopped after %d bytes\n",
                    stream_ctx->stream_id, stream_ctx->command, (int)stream_ctx->received_length);
            }
            stream_ctx = stream_ctx->next_stream;
        }

        return 0;
    }

    /* if stream is already present, check its state. New bytes? */
    while (stream_ctx != NULL && stream_ctx->stream_id != stream_id) {
        stream_ctx = stream_ctx->next_stream;
    }

    if (stream_ctx == NULL || stream_ctx->F == NULL) {
        /* Unexpected stream. */
//        printf("unexpected stream !\n");
//        picoquic_reset_stream(cnx, stream_id, 0);
        stream_ctx = demo_client_open_receive_stream(cnx, ctx, stream_id);
    } else if (fin_or_event == picoquic_callback_stream_reset) {
        picoquic_reset_stream(cnx, stream_id, 0);

        if (stream_ctx->F != NULL) {
            char buf[256];

            if (stream_ctx->F != dev_null) {
                fclose(stream_ctx->F);
            }
            stream_ctx->F = NULL;
            ctx->nb_open_streams--;

            fprintf(stdout, "Reset received on stream %u, command: %s, after %d bytes\n",
                stream_ctx->stream_id,
                strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command),
                (int)stream_ctx->received_length);
        }
        return 0;
    } else if (fin_or_event == picoquic_callback_stop_sending) {
        char buf[256];
        picoquic_reset_stream(cnx, stream_id, 0);

        fprintf(stdout, "Stop sending received on stream %u, command: %s\n",
            stream_ctx->stream_id,
            strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command));
        return 0;
    } else if (fin_or_event == picoquic_callback_stream_gap) {
        /* We do not support this, yet */
        picoquic_reset_stream(cnx, stream_id, PICOQUIC_TRANSPORT_PROTOCOL_VIOLATION);
        return 0;
    }
    printf("EVENT::{\"time\": %ld, \"type\": \"stream_deliver\", \"stream_id\": %ld, \"range\": [%lu, %lu]}\n", picoquic_current_time(), stream_id, stream_ctx->received_length, length);
    if (fin_or_event == picoquic_callback_no_event || fin_or_event == picoquic_callback_stream_fin) {
        if (length > 0 && stream_id != handshake_stream_id && ( (stream_id != control_stream_id && stream_id != control_stream_id + 4 && stream_id != server_video_stats_feedback_stream_id && stream_id != server_video_control_stream_id))) {
            stream_receiver_receive_data(stream_ctx->stream_receiver, bytes, length, current_time);
            if (stream_ctx->stream_receiver->summary.contains_full_message) {
                if (first_fully_received_frame_timestamp == 0) {
                    first_fully_received_frame_timestamp = current_time;
                }
                ssize_t size = stream_receiver_get_message_payload(stream_ctx->stream_receiver, data_buffer);
                printf("get full message of size %ld, sync = %lu, now = %lu, now relative in ms = %lu\n", size,
                       stream_ctx->stream_receiver->summary.current_message_synchro_timestamp_ms,
                       stream_ctx->stream_receiver->summary.current_message_reception_timestamp_us, (current_time - first_fully_received_frame_timestamp) / 1000);
#ifndef DISABLE_FFMPEG
                if (!playback) {
                    AVPacket *packet = av_packet_alloc();
                    av_packet_from_data(packet, data_buffer, size);

                    AVFrame *pFrame = av_frame_alloc();
                    if (!pFrame) {
                        printf("failed to allocated memory for AVFrame\n");
                        exit(-1);
                    }
                    char type = 0;
                    int err = decode_packet(packet, stream_ctx->pCodecContext, pFrame, &type);
                    if (err) {
                        printf("error while decoding\n");
                        exit(-1);
                    }
                }
#endif
            }
            (void)fwrite(bytes, 1, length, stream_ctx->F);
            stream_ctx->received_length += length;

        } else if (stream_id == server_video_stats_feedback_stream_id) {
            if (!ctx->video_stats_file) {
                ctx->video_stats_file = fopen(video_stats_filename, "w");
            }
            if (!ctx->video_stats_file) {
                printf("error while opening video stats filename\n");
                exit(-1);
            }
            fwrite(bytes, length, 1, ctx->video_stats_file);
            if (fin_or_event == picoquic_callback_stream_fin) {
                fclose(ctx->video_stats_file);
                picoquic_close(cnx, 0);
                ctx->stream_ok = true;  // to finish correctly the download
            }
        }

        /* if FIN present, process request through http 0.9 */
        if (fin_or_event == picoquic_callback_stream_fin) {
            char buf[256];
            if (stream_id == 4 || (post_request && stream_id == server_video_stats_feedback_stream_id)) {
                gettimeofday(&ctx->tv_end, NULL);
                ctx->stream_ok = true;
            }
            /* if data generated, just send it. Otherwise, just FIN the stream. */
            if (stream_ctx->F != dev_null) {
                fclose(stream_ctx->F);
            }
            stream_ctx->F = NULL;
            ctx->nb_open_streams--;
            fin_stream_id = stream_id;

            printf("Received file %s, after %d bytes, closing stream %u\n",
                strip_endofline(buf, sizeof(buf), (char*)&stream_ctx->command[4]),
                (int)stream_ctx->received_length, stream_ctx->stream_id);
            printf("clean stream receiver\n");
            stream_ctx->receiver_summary = stream_ctx->stream_receiver->summary;
            free(stream_ctx->stream_receiver);
            stream_ctx->stream_receiver = NULL;
        }
    }

    if (fin_stream_id != 0xFFFFFFFF) {
        demo_client_start_streams(cnx, ctx, fin_stream_id);
    }
    if (fin_stream_id == 0) {
        init_video_streamer(cnx, 4, current_time, ctx->response_length, 0);
    }

    /* that's it */
    return 0;
}

#define PICOQUIC_DEMO_CLIENT_MAX_RECEIVE_BATCH 4

int quic_client(const char* ip_address_text, int server_port, const char * sni,
                const char * root_crt,
                uint32_t proposed_version, int force_zero_share, int mtu_max, FILE* F_log, FILE* F_tls_secrets,
                const char** local_plugin_fnames, int local_plugins,
                int request_size, int only_stream_4, char *qlog_filename, char *plugin_store_path, char *stats_filename)
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

    streamers_buffer_slot_t current_streamers[MAX_STREAMERS_BUFFER_SIZE];

    memset(&callback_ctx, 0, sizeof(picoquic_first_client_callback_ctx_t));

    ret = picoquic_get_server_address(ip_address_text, server_port, &server_address, &server_addr_length, &is_name);
    if (sni == NULL && is_name != 0) {
        sni = ip_address_text;
    }

    /* Open a UDP socket */

    if (ret == 0) {
        /* Make the most possible flexible socket */
        fd = socket(server_address.ss_family, SOCK_DGRAM, IPPROTO_UDP);
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

    picoquic_tp_t tp;
    memset(&tp, 0, sizeof(picoquic_tp_t));
    /* Create the client connection */
    if (ret == 0) {
        picoquic_init_transport_parameters(&tp, 1);
        // ensure that the receive window is respected at the beginning of the transfer
        tp.initial_max_stream_data_bidi_local = MIN(MAX(initial_receive_window_size, tp.initial_max_stream_data_bidi_local), max_stream_receive_window_size);
        tp.initial_max_stream_data_bidi_remote = MIN(MAX(initial_receive_window_size, tp.initial_max_stream_data_bidi_remote), max_stream_receive_window_size);
        tp.initial_max_stream_data_uni = MIN(MAX(initial_receive_window_size, tp.initial_max_stream_data_uni), max_stream_receive_window_size);
        tp.initial_max_data = MAX(initial_receive_window_size, tp.initial_max_data);
        qclient->default_tp = &tp;
        /* Create a client connection */
        cnx_client = picoquic_create_cnx_with_transport_parameters(qclient, picoquic_null_connection_id, picoquic_null_connection_id,
                                                                   (struct sockaddr*)&server_address, current_time,
                                                                   proposed_version, sni, alpn, 1, tp);

        if (cc_algorithm) {
            picoquic_set_congestion_algorithm(cnx_client, cc_algorithm);
        }
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

            if (ret == 0) {
                if (picoquic_is_0rtt_available(cnx_client) && (proposed_version & 0x0a0a0a0a) != 0x0a0a0a0a) {
                    zero_rtt_available = 1;

                    /* Queue a simple frame to perform 0-RTT test */
                    /* Start the download scenario */
                    /* If request_size is greater than 0, select it */
                    if (request_size <= 0) {
                        callback_ctx.demo_stream = test_scenario;
                        callback_ctx.nb_demo_streams = test_scenario_nb;
                    } else {
                        sprintf(buf, "doc-%d.html", request_size);
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
                            perror("cannot sendto\n");
                            fprintf(F_log, "Cannot send first packet to server, returns %d, addr_family = %d\n", bytes_sent, server_address.ss_family);
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

        current_time = picoquic_current_time();
        int has_active_streamer = 0;

        int n_streamers = get_all_streamers(&streamers_buffer, current_streamers);

        uint64_t smallest_timestamp = ~((uint64_t) 0);

        for (int i = 0 ; i < n_streamers ; i++) {
            timed_video_streamer_t *streamer = (timed_video_streamer_t *) current_streamers[i].streamer;
            if (!timed_video_streamer_is_finished(streamer)) {
                has_active_streamer = 1;
                uint64_t next_sending_timestamp = timed_video_get_next_sending_timestamp(playback, streamer);
                if (smallest_timestamp > next_sending_timestamp) {
                    smallest_timestamp = next_sending_timestamp;
                }
            }
        }

        if (smallest_timestamp > current_time) {
            delta_t = (delta_t < (smallest_timestamp - current_time)) ? delta_t : (smallest_timestamp - current_time);
        } else if (has_active_streamer) {
            delta_t = 0;
        }

        printf("delta_t = %ld\n", delta_t);

        from_length = to_length = sizeof(struct sockaddr_storage);

        bytes_recv = picoquic_select(&fd, 1, &packet_from, &from_length,
            &packet_to, &to_length, &if_index_to,
            buffer, sizeof(buffer),
            delta_t,
            &current_time,
            qclient);

        enqueue_streamers_messages(n_streamers, current_streamers, current_time, 0);

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

                        if (fixed_cwin != 0) {
                            set_fixed_cwin(cnx_client, fixed_cwin);
                        }

                        if (zero_rtt_available == 0) {
                            /* Start the download scenario */
                            /* If request_size is greater than 0, select it */
                            if (request_size <= 0) {
                                callback_ctx.demo_stream = test_scenario;
                                callback_ctx.nb_demo_streams = test_scenario_nb;
                            } else {
                                if (post_request) {
                                    // post video
                                    sprintf(buf, "video-upload");
                                } else {
                                    sprintf(buf, "doc-%d.html", request_size);
                                }
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

                            if (post_request) {
                                callback_ctx.response_length = request_size;
                            }
                            demo_client_start_streams(cnx_client, &callback_ctx, 0xFFFFFFFF);
                        }
                    }

                    client_ready_loop++;

                    if ((bytes_recv == 0 || client_ready_loop > 4) && picoquic_is_cnx_backlog_empty(cnx_client)) {
                        if (callback_ctx.nb_open_streams == 0 || callback_ctx.stream_ok) {
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

                            if (!post_request) {
                                picoquic_first_client_stream_ctx_t *stream_ctx = callback_ctx.first_stream;
                                FILE *video_stats_file = stdout;
                                if (video_stats_filename) {
                                    video_stats_file = fopen(video_stats_filename, "w");
                                    if (!video_stats_file) {
                                        fprintf(stderr, "could not open video stats file: %s\n", strerror(errno));
                                        video_stats_file = stdout;
                                    }
                                }
                                fprintf(video_stats_file, "stream_id,size,frame_timestamp_ms,reception_timestamp_ms\n");
                                while (stream_ctx) {
                                    if (stream_ctx->receiver_summary.contains_full_message) {
                                        fprintf(video_stats_file, "%u,%ld,%ld,%ld\n", stream_ctx->stream_id,
                                                stream_ctx->receiver_summary.message_size,
                                                stream_ctx->receiver_summary.current_message_synchro_timestamp_ms,
                                                stream_ctx->receiver_summary.current_message_reception_timestamp_us/1000);
                                    }
                                    stream_ctx = stream_ctx->next_stream;
                                }
                            }

                            if (request_size > 0) {
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
    if (request_size > 0) {
        if (callback_ctx.stream_ok) {
            int time_us = (callback_ctx.tv_end.tv_sec - callback_ctx.tv_start.tv_sec) * 1000000 + callback_ctx.tv_end.tv_usec - callback_ctx.tv_start.tv_usec;
            fprintf(stdout,"%d.%03d ms\n", time_us / 1000, time_us % 1000);
        } else {
            fprintf(stdout,"-1.0\n");
        }
        fprintf(stdout, "done\n");
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
    fprintf(stderr, "Usage: picoquicvideo [server_name [port]] <options>\n");
    fprintf(stderr, "  For the client mode, specify sever_name and port.\n");
    fprintf(stderr, "  For the server mode, use -p to specify the port.\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c file               cert file (default: %s)\n", default_server_cert_file);
    fprintf(stderr, "  -k file               key file (default: %s)\n", default_server_key_file);
    fprintf(stderr, "  -G size               GET size (default: -1)\n");
    fprintf(stderr, "  -U size               upload instead of download video\n");
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
    fprintf(stderr, "  -w stream_rwin_max    sets the maximum value in bytes for the size of the streams receive window\n");
    fprintf(stderr, "  -E stream_rwin_max    sets the initial value in bytes for the size of the streams receive window\n");
    fprintf(stderr, "  -S filename           if set, write plugin statistics in the specified file (- for stdout)\n");
    fprintf(stderr, "  -w stream_rwin_max    sets the maximum value in bytes for the size of the streams receive window\n");
    fprintf(stderr, "  -E stream_rwin_max    sets the initial value in bytes for the size of the streams receive window\n");
    fprintf(stderr, "  -V filename           filename of the video to transmit)\n");
    fprintf(stderr, "  -O filename           filename of the video video frames reception summary\n");
    fprintf(stderr, "  -a                    if set, use the API provided by the FEC window framework\n");
    fprintf(stderr, "  -F repair_receive_window_size               sets the size of the buffer allocated to store repair symbols when using FEC\n");
    fprintf(stderr, "  -A bbr|cubic|newreno               sets the used congestion control algorithm\n");
    fprintf(stderr, "  -D deadline_ms               sets delivery deadline for each sent video frame in milliseconds\n");
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
    setbuf(stdout, NULL);

#ifdef _WINDOWS
    WSADATA wsaData;
#endif
    int ret = 0;

    /* HTTP09 test */
    int request_size = -1;
    int only_stream_4 = 0;

    char *qlog_filename = NULL;
    char *stats_filename = NULL;

    /* Get the parameters */
    int opt;
//    while ((opt = getopt(argc, argv, "c:k:P:C:Q:G:U:p:v:L14rhzRJX:S:i:s:l:m:n:t:q:w:E:N:I:W:F:A:")) != -1) //{
    while ((opt = getopt(argc, argv, "c:k:P:C:Q:G:U:p:v:L14rhzRaV:X:S:i:d:u:s:l:m:n:t:q:w:E:W:O:A:D:H:")) != -1) {
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
        case 'U':
            post_request = true;
            if ((request_size = atoi(optarg)) <= 0) {
                fprintf(stderr, "Invalid POST size: %s\n", optarg);
                usage();
            }
            break;
        case 'G':
            if ((request_size = atoi(optarg)) <= 0) {
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
            message_interval_microsec = strtol(optarg, NULL, 10);
            if (message_interval_microsec == LONG_MIN || message_interval_microsec == LONG_MAX) {
                fprintf(stderr, "bad interval value: %s\n", strerror(errno));
                usage();
            }
            break;
        case 'd':
            message_size_std_dev = strtol(optarg, NULL, 10);
            if (message_size_std_dev == LONG_MIN || message_size_std_dev == LONG_MAX) {
                fprintf(stderr, "bad message size std dev value: %s\n", strerror(errno));
                usage();
            }
            break;
        case 'u':
            mean_message_size = strtol(optarg, NULL, 10);
            if (mean_message_size == LONG_MIN || mean_message_size == LONG_MAX) {
                fprintf(stderr, "mean message size value: %s\n", strerror(errno));
                usage();
            }
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
        case 'V':
            video_filename = optarg;
            char *extension = strrchr(video_filename, '.');
            if (extension && strcmp(extension, ".repr") == 0) {
                // filed ending with .repr are playback files
                playback = true;
            }
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
        case 'E':
            initial_receive_window_size = atol(optarg);
            if (initial_receive_window_size <= 0) {
                fprintf(stderr, "Invalid max stream receive window size: %s\n", optarg);
                usage();
            }
            printf("SET RWIN TO %lu BYTES\n", max_stream_receive_window_size);
            break;
        case 'a':
            use_pquic_fec_messages_api = true;
            break;
        case 'W':
            fixed_cwin = atol(optarg);
            break;
        case 'O':
            video_stats_filename = optarg;
            break;
        case 'F':
            repair_receive_window_size = atol(optarg);
            printf("SET REPAIR RWIN %lu\n", repair_receive_window_size);
            break;
        case 'A':
            // cc algorithm
            if (strcasecmp(optarg, "bbr") == 0) {
                cc_algorithm = picoquic_bbr_algorithm;
            } else if (strcasecmp(optarg, "cubic") == 0) {
                cc_algorithm = picoquic_cubic_algorithm;
            } else if (strcasecmp(optarg, "newreno") == 0) {
                cc_algorithm = picoquic_newreno_algorithm;
            } else {
                fprintf(stderr, "Invalid cc algorithm: %s\n", optarg);
                usage();
            }
            break;
        case 'D':
            // deadline for messages
            messages_deadline_microsec = atol(optarg) * 1000;
            if (messages_deadline_microsec < 0) {
                fprintf(stderr, "Invalid messages deadline: %s\n", optarg);
                usage();
            }
            break;
        case 'H':
            // handshake_response_size
            handshake_response_length = atol(optarg);
            if (handshake_response_length < 0 || handshake_response_length > 1000000) {
                fprintf(stderr, "handshake response length: %s (should be > 0 and <= 1000000\n", optarg);
                usage();
            }
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
    printf("EVENT::{\"time\": %ld, \"type\": \"init\"}\n", picoquic_current_time());
    fflush(stdout);
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

    if (!F_log && (!log_file || strcmp(log_file, "/dev/null") != 0)) {
        F_log = stdout;
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

    dev_null = fopen("/dev/null", "wb");
    if (!dev_null) {
        fprintf(stdout, "Cannot open file: /dev/null: %s\n", strerror(errno));
        exit(-1);
    }

    streamers_buffer_init(&streamers_buffer);

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
            NULL, NULL, (uint8_t*)reset_seed, mtu_max, local_plugin_fnames, local_plugins,
            both_plugin_fnames, both_plugins, F_log, F_tls_secrets, qlog_filename, stats_filename, preload_plugins);
        fprintf(stdout,"Server exit with code = %d\n", ret);
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
        ret = quic_client(server_name, server_port, sni, root_trust_file, proposed_version, force_zero_share, mtu_max, F_log, F_tls_secrets, local_plugin_fnames, local_plugins, request_size, only_stream_4, qlog_filename, plugin_store_path, stats_filename);

        fprintf(stdout,"Client exit with code = %d\n", ret);

        if (F_log != NULL && F_log != stdout) {
            fclose(F_log);
        }
        if (F_tls_secrets != NULL && F_tls_secrets != stdout) {
            fclose(F_tls_secrets);
        }
        if (dev_null) {
            fclose(dev_null);
        }
    }
}

