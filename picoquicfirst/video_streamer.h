
#ifndef PICOQUIC_VIDEM_STREAMER_H
#define PICOQUIC_VIDEO_STREAMER_H
#include "streamer.h"

#ifndef DISABLE_FFMPEG
#if (defined NS3 || defined ANDROID || defined __ANDROID__)
#define DISABLE_FFMPEG
#endif
#endif

#ifndef DISABLE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#endif

#include <stdbool.h>


#define VIDEO_BUFFER_MAX_SIZE 1000000       // 1MB max for a frame

typedef struct {
    streamer_t simple_streamer;

    uint8_t padding[1000 + VIDEO_BUFFER_MAX_SIZE];
} abstract_video_streamer_t;
typedef struct {
    streamer_t simple_streamer;
    int64_t interval_microsec;        // send one frame per interval
    int64_t _last_sent_time_microsec; // last timestamp at which a message has been sent
    int video_stream_idx;
    bool finished;
    bool has_sent_first_frame;
    uint64_t first_sent_timestamp;

} timed_video_streamer_t;

#ifndef DISABLE_FFMPEG
typedef struct {
    timed_video_streamer_t streamer;

    AVFormatContext *pFormatContext;

    uint8_t buffer[VIDEO_BUFFER_MAX_SIZE];
} timed_real_video_streamer_t;
#endif

typedef struct {
    uint64_t timestamp_since_origin;
    int64_t  video_frame_size;
    char video_frame_type;
} frame_repr_t;

typedef struct {
    timed_video_streamer_t streamer;

    int current_frame;

    int n_frames;
    frame_repr_t *video_frames;

    uint8_t buffer[VIDEO_BUFFER_MAX_SIZE];
} timed_playback_video_streamer_t;

#ifndef DISABLE_FFMPEG
int timed_real_video_streamer_init(timed_real_video_streamer_t *streamer, uint64_t stream_id, AVFormatContext *pFormatContext, int video_stream_idx, uint64_t frames_interval_microsec, size_t max_size) {
    memset(streamer, 0, sizeof(timed_real_video_streamer_t));
    streamer->streamer.simple_streamer.current_index = 0;
    streamer->streamer.simple_streamer.total_response_size = max_size;
    streamer->streamer.simple_streamer.stream_id = stream_id;
    streamer->streamer.video_stream_idx = video_stream_idx;
    streamer->streamer.interval_microsec = frames_interval_microsec;
    streamer->pFormatContext = pFormatContext;
}
#endif


int timed_playback_video_streamer_init(timed_playback_video_streamer_t *streamer, uint64_t stream_id, char *video_representation_filename, size_t max_size) {
    memset(streamer, 0, sizeof(timed_playback_video_streamer_t));
    streamer->streamer.simple_streamer.current_index = 0;
    streamer->streamer.simple_streamer.total_response_size = max_size;
    streamer->streamer.simple_streamer.stream_id = stream_id;
    FILE *video = fopen(video_representation_filename, "r");
    if (!video) {
        fprintf(stdout, "cannot open video: %s", strerror(errno));
        return -1;
    }
    fprintf(stdout, "parsing playback repr\n");
    int current_max = 1000;
    streamer->n_frames = 0;
    streamer->video_frames = malloc(current_max*sizeof(timed_playback_video_streamer_t));
    if (!streamer->video_frames) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }
    int ret;
    int64_t current_relative_timestamp = 0;
    int64_t current_frame_size = 0;
    char type = '?';
    while((ret = fscanf(video, "%c%ld,%ld\n", &type, &current_relative_timestamp, &current_frame_size)) == 3) {
        streamer->video_frames[streamer->n_frames++] = (frame_repr_t) {.video_frame_type = type, .timestamp_since_origin = current_relative_timestamp, .video_frame_size = current_frame_size};
        if (streamer->n_frames == current_max) {
            current_max *= 2;
            streamer->video_frames = realloc(streamer->video_frames, current_max*sizeof(frame_repr_t));
            if (!streamer->video_frames) {
                fprintf(stderr, "out of memory\n");
                return -1;
            }
        }
//        if (streamer->n_frames == 2) {
//            streamer->streamer.interval_microsec = current_relative_timestamp - streamer->video_frames[0].timestamp_since_origin;
//        }
    }
    if (ret != EOF) {
        fprintf(stdout, "could not read file: %s\n", strerror(ferror(video)));
    }
    fprintf(stdout, "n_frames = %d\n", streamer->n_frames);
    return 0;
}

int timed_video_streamer_is_finished(timed_video_streamer_t *streamer) {
    return streamer->finished || (streamer->simple_streamer.current_index >= streamer->simple_streamer.total_response_size);
}

#ifndef DISABLE_FFMPEG

size_t timed_real_video_streamer_should_send_now(timed_real_video_streamer_t *streamer, int64_t current_time) {
    return current_time - streamer->streamer._last_sent_time_microsec >= streamer->streamer.interval_microsec;
}

size_t timed_real_video_streamer_next_timestamp(timed_real_video_streamer_t *streamer) {
    return streamer->streamer._last_sent_time_microsec + streamer->streamer.interval_microsec;
}

#endif

size_t timed_playback_video_streamer_should_send_now(timed_playback_video_streamer_t *streamer, int64_t current_time) {
    return !streamer->streamer.has_sent_first_frame || current_time >= streamer->streamer.first_sent_timestamp + streamer->video_frames[streamer->current_frame].timestamp_since_origin;
}

size_t timed_playback_video_streamer_next_timestamp(timed_playback_video_streamer_t *streamer) {
    return streamer->streamer.first_sent_timestamp + streamer->video_frames[streamer->current_frame].timestamp_since_origin;
}

size_t timed_video_streamer_should_send_now(bool playback, abstract_video_streamer_t *streamer, int64_t current_time) {
#ifndef DISABLE_FFMPEG
    if (!playback) {
        return timed_real_video_streamer_should_send_now((timed_real_video_streamer_t *) streamer, current_time);
    }
#endif
    return timed_playback_video_streamer_should_send_now((timed_playback_video_streamer_t *) streamer, current_time);
}

size_t timed_video_streamer_next_timestamp(bool playback, abstract_video_streamer_t *streamer) {

#ifndef DISABLE_FFMPEG
    if (!playback) {
        return timed_real_video_streamer_should_send_now((timed_real_video_streamer_t *) streamer, current_time);
    }
#endif
    return timed_playback_video_streamer_next_timestamp((timed_playback_video_streamer_t *) streamer);
}

#ifndef DISABLE_FFMPEG
int get_next_video_packet(AVFormatContext *pFormatContext, AVPacket *pPacket, int video_stream_index) {
    int response = 0;
    while ((response = av_read_frame(pFormatContext, pPacket)) >= 0)
    {
        // if it's the video stream
        if (pPacket->stream_index == video_stream_index) {
            printf("AVPacket->pts %" PRId64, pPacket->pts);

            if (response < 0)
                break;
            return response;
        }
        // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
    }
}


size_t timed_real_video_streamer_get_stream_bytes(timed_real_video_streamer_t *streamer, uint8_t **buffer, int64_t current_time, AVPacket *packet) {
    if (timed_real_video_streamer_should_send_now(streamer, current_time) && !timed_video_streamer_is_finished(&streamer->streamer)) {
        streamer->streamer._last_sent_time_microsec = current_time;
        int response = get_next_video_packet(streamer->pFormatContext, packet, streamer->streamer.video_stream_idx);
        if (response >= 0) {
            if (!streamer->streamer.has_sent_first_frame) {
                streamer->streamer.has_sent_first_frame = true;
                streamer->streamer.first_sent_timestamp = current_time;
            }
            size_t size = packet->size + sizeof(uint32_t);
            printf("get %d stream video bytes\n", packet->size);
            uint32_t size_n = htonl(packet->size);
            memcpy(streamer->buffer, &size_n, sizeof(size_n));
            uint32_t timestamp = htonl((uint32_t) ((current_time - streamer->streamer.first_sent_timestamp)/1000));
            memcpy(&streamer->buffer[sizeof(size_n)], &timestamp, sizeof(timestamp));
            memcpy(&streamer->buffer[sizeof(size_n) + sizeof(timestamp)], packet->data, packet->size);
            streamer->streamer.simple_streamer.current_index += size;
            *buffer = streamer->buffer;
            return size;
        } else if (response == AVERROR_EOF) {
            streamer->streamer.finished = true;
            return 0;
        }
        return -1;
    }
    return 0;
}
#endif

size_t timed_playback_video_streamer_get_stream_bytes(timed_playback_video_streamer_t *streamer, uint8_t **buffer, int64_t current_time, char *type) {
    if (timed_playback_video_streamer_should_send_now(streamer, current_time) && !timed_video_streamer_is_finished(&streamer->streamer)) {
        streamer->streamer._last_sent_time_microsec = current_time;
        if (streamer->current_frame < streamer->n_frames) {
            if (!streamer->streamer.has_sent_first_frame) {
                streamer->streamer.has_sent_first_frame = true;
                streamer->streamer.first_sent_timestamp = current_time;
            }
            frame_repr_t current_frame = streamer->video_frames[streamer->current_frame++];
            *type = current_frame.video_frame_type;
            size_t size = current_frame.video_frame_size + sizeof(uint32_t) + sizeof(uint32_t);
            printf("get %ld stream video bytes\n", current_frame.video_frame_size);
            uint32_t size_n = htonl(current_frame.video_frame_size);
            uint32_t timestamp = htonl((uint32_t) ((current_time - streamer->streamer.first_sent_timestamp)/1000));
            memcpy(streamer->buffer, &size_n, sizeof(size_n));
            streamer->streamer.simple_streamer.current_index += size;
            memcpy(&streamer->buffer[sizeof(size_n)], &timestamp, sizeof(timestamp));
            *buffer = streamer->buffer;
            return size;
        } else if (streamer->current_frame == streamer->n_frames) {
            streamer->streamer.finished = true;
            return 0;
        }
        return -1;
    }
    return 0;
}
#ifndef DISABLE_FFMPEG
uint64_t timed_real_video_get_next_sending_timestamp(timed_real_video_streamer_t *streamer) {
    printf("INTERVAL MICROSEC = %ld\n", streamer->streamer.interval_microsec);
    return streamer->streamer._last_sent_time_microsec + streamer->streamer.interval_microsec;
}
#endif

uint64_t timed_playback_video_get_next_sending_timestamp(timed_playback_video_streamer_t *streamer) {
    return streamer->streamer.first_sent_timestamp + streamer->video_frames[streamer->current_frame].timestamp_since_origin;
}

uint64_t timed_video_get_next_sending_timestamp(bool playback, abstract_video_streamer_t *streamer) {
#ifndef DISABLE_FFMPEG
    if (!playback) {
        return timed_real_video_get_next_sending_timestamp((timed_real_video_streamer_t *) streamer);
    }
#endif
    return timed_playback_video_get_next_sending_timestamp((timed_playback_video_streamer_t *) streamer);
}

typedef struct {
    uint64_t current_message_synchro_timestamp_ms;
    uint64_t current_message_reception_timestamp_us;
    size_t message_size;
    bool contains_full_message;
} receiver_summary_t;

// the stream_receiver assumes that at the beginning of every message, there is its length network-encoded on 4 bytes
typedef struct {
    int64_t current_message_size;
    int64_t current_message_offset;
    receiver_summary_t summary;
    uint8_t buffer[VIDEO_BUFFER_MAX_SIZE];

} stream_receiver_t;

int stream_receiver_init(stream_receiver_t *receiver) {
    memset(receiver, 0, sizeof(stream_receiver_t));
}

int stream_receiver_receive_data(stream_receiver_t *receiver, uint8_t *data, size_t size, uint64_t current_time) {
    if (size + receiver->current_message_offset > VIDEO_BUFFER_MAX_SIZE || receiver->summary.contains_full_message)
        return -1;

    if (receiver->current_message_offset == 0) {
        receiver->current_message_size = ntohl(*((uint32_t *) data));
        printf("RECEIVE START OF MESSAGE OF SIZE %ld\n", receiver->current_message_size);
        data += sizeof(uint32_t);
        size -= sizeof(uint32_t);
        receiver->summary.current_message_synchro_timestamp_ms = ntohl(*((uint32_t *) data));
        data += sizeof(uint32_t);
        size -= sizeof(uint32_t);
    }
    memcpy(&receiver->buffer[receiver->current_message_offset], data, size);
    receiver->current_message_offset += size;

    bool contains_full_message = receiver->summary.contains_full_message;

    receiver->summary.contains_full_message = receiver->current_message_offset == receiver->current_message_size;
    receiver->summary.message_size = receiver->current_message_size;

    if (!contains_full_message && receiver->summary.contains_full_message) {
        receiver->summary.current_message_reception_timestamp_us = current_time;
    }
}

// pre: space available in datA_buffer >= VIDEO_BUFFER_MAX_SIZE
ssize_t stream_receiver_get_message_payload(stream_receiver_t *receiver, uint8_t *data_buffer) {
    if (!receiver->summary.contains_full_message)
        return -1;
    memcpy(data_buffer, receiver->buffer, receiver->current_message_offset);
    ssize_t size = receiver->current_message_offset;

    printf("receive %ld bytes message\n", size);
    return size;
}


static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}
#ifndef DISABLE_FFMPEG
int n_frames_to_save = 0;
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame, char *type)
{
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
    int response = avcodec_send_packet(pCodecContext, pPacket);

    if (response < 0) {
        printf("Error while sending a packet to the decoder: %s\n", av_err2str(response));
        return response;
    }

    while (response >= 0)
    {
        printf("decoded packet, response = %d\n", response);
        // Return decoded output data (into a frame) from a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
        response = avcodec_receive_frame(pCodecContext, pFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            printf("Error while receiving a frame from the decoder: %s\n", av_err2str(response));
            return response;
        }

        if (response >= 0) {
            *type = av_get_picture_type_char(pFrame->pict_type);
            printf(
                    "Frame %d (type=%c, size=%d bytes) pts %d key_frame %d [DTS %d]\n",
                    pCodecContext->frame_number,
                    *type,
                    pFrame->pkt_size,
                    pFrame->pts,
                    pFrame->key_frame,
                    pFrame->coded_picture_number
            );

            char frame_filename[1024];
            snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
            // save a grayscale frame into a .pgm file
            if (--n_frames_to_save >= 0)
                save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);
        }
    }
    return 0;
}
#endif



#endif //PICOQUIC_VIDEO_STREAMER_H
