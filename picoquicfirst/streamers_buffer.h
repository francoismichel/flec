
#ifndef PICOQUIC_STREAMERS_BUFFER_H
#define PICOQUIC_STREAMERS_BUFFER_H

#include "streamer.h"

#define MAX_STREAMERS_BUFFER_SIZE 100

typedef struct {
    streamer_t *streamer;
    void *streamer_ctx;
} streamers_buffer_slot_t;

typedef struct {
    int start_index;
    int size;
    streamers_buffer_slot_t slots[MAX_STREAMERS_BUFFER_SIZE];
} st_streamers_buffer_t;

typedef st_streamers_buffer_t streamers_buffer_t;

int streamers_buffer_init(streamers_buffer_t *buffer) {
    buffer->size = 0;
    buffer->start_index = 0;
    return 0;
}

int streamers_buffer_add_streamer(streamers_buffer_t *buffer, streamer_t *streamer, void *context) {
    if (buffer->size >= MAX_STREAMERS_BUFFER_SIZE)
        return -1; // could not add

    int free_idx = (buffer->start_index + buffer->size++) % MAX_STREAMERS_BUFFER_SIZE;
    buffer->slots[free_idx].streamer = streamer;
    buffer->slots[free_idx].streamer_ctx = context;
    return 0;
}

// pre: the buffer has at least buffer->size*sizeof(streamers_buffer_slot_t) bytes
int get_all_streamers(streamers_buffer_t *buffer, streamers_buffer_slot_t *streamers) {
    for (int i = 0 ; i < buffer->size ; i++) {
        streamers[i] = buffer->slots[(buffer->start_index + i) % MAX_STREAMERS_BUFFER_SIZE];
    }
    return buffer->size;
}

#endif //PICOQUIC_STREAMERS_BUFFER_H
