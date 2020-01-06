
#ifndef PICOQUIC_STREAMER_H
#define PICOQUIC_STREAMER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    uint64_t stream_id;
    uint8_t *response;
    size_t total_response_size;
    size_t current_index;
} streamer_t;

typedef struct {
    uint32_t mean;
    double stddev;
    unsigned int seed;
} gaussian_t;

typedef struct {
    streamer_t streamer;
    gaussian_t gaussian;
} gaussian_streamer_t;

typedef struct {
    gaussian_streamer_t gaussian_streamer;
    int64_t interval_microsec; // send one gaussian-determined message per interval
    int64_t _last_sent_time_microsec; // last timestamp at which a message has been sent
} timed_gaussian_streamer_t;

double get_random_gaussian(gaussian_t *gaussian) {
    double total = 0.0;
    for (int i = 0 ; i < 12 ; i++) {
        total += (double) rand_r(&gaussian->seed) / (double) RAND_MAX; // so that the random value is [0, 1]
    }
    total -= 6.0;
    return total*gaussian->stddev + gaussian->mean;
}

/**
 *  Initializes a gaussian streamer streaming chunks of mean size mean_chunk_size bytes, with a standard deviation of
 *  chunk_size_stddev bytes. It takes the specified seed as the seed of its random number generator and will serve
 *  stream chunks coming from the response buffer.
 * @return 0 if everything went well, 1 otherwise
 */
int gaussian_streamer_init(gaussian_streamer_t *streamer, uint64_t stream_id, const uint8_t *response, size_t total_response_size, size_t mean_chunk_size, size_t chunk_size_stddev, unsigned int seed) {
    memset(streamer, 0, sizeof(gaussian_streamer_t));
    streamer->streamer.response = response;
    streamer->streamer.total_response_size = total_response_size;
    streamer->streamer.current_index = 0;
    streamer->streamer.stream_id = stream_id;

    streamer->gaussian.mean = mean_chunk_size;
    streamer->gaussian.stddev = chunk_size_stddev;
    streamer->gaussian.seed = seed;
    return 0;
}

/**
 * returns a random number of bytes from the streamer that follow its distribution
 */
size_t gaussian_streamer_get_stream_nbytes(gaussian_streamer_t *streamer) {
    size_t n_bytes = (size_t) get_random_gaussian(&streamer->gaussian);
    if (streamer->streamer.current_index + n_bytes > streamer->streamer.total_response_size)
        n_bytes = streamer->streamer.total_response_size - streamer->streamer.current_index;
    streamer->streamer.current_index += n_bytes;
    return n_bytes;
}

/**
 * returns a stream chunk of a random number of bytes from the streamer that follow its distribution
 */
size_t gaussian_streamer_get_stream_bytes(gaussian_streamer_t *streamer, uint8_t **buffer) {
    size_t n_bytes = (size_t) get_random_gaussian(&streamer->gaussian);
    printf("got random %lu, current_index = %lu, total_size = %lu\n", n_bytes, streamer->streamer.current_index, streamer->streamer.total_response_size);
    if (streamer->streamer.current_index + n_bytes > streamer->streamer.total_response_size)
        n_bytes = streamer->streamer.total_response_size - streamer->streamer.current_index;
    *buffer = &streamer->streamer.response[streamer->streamer.current_index];
    streamer->streamer.current_index += n_bytes;
    printf("n_bytes = %lu\n", n_bytes);
    return n_bytes;
}

int gaussian_streamer_is_finished(gaussian_streamer_t *streamer) {
    return streamer->streamer.current_index == streamer->streamer.total_response_size;
}

size_t timed_gaussian_streamer_should_send_now(timed_gaussian_streamer_t *streamer, int64_t current_time) {
    return current_time - streamer->_last_sent_time_microsec > streamer->interval_microsec;
}

size_t timed_gaussian_streamer_get_stream_bytes(timed_gaussian_streamer_t *streamer, uint8_t **buffer, int64_t current_time) {
    if (timed_gaussian_streamer_should_send_now(streamer, current_time) && !gaussian_streamer_is_finished(&streamer->gaussian_streamer)) {
        streamer->_last_sent_time_microsec = current_time;
        printf("get stream bytes\n");
        return gaussian_streamer_get_stream_bytes(&streamer->gaussian_streamer, buffer);
    }
    return 0;
}

uint64_t timed_gaussian_get_next_sending_timestamp(timed_gaussian_streamer_t *streamer) {
    return streamer->_last_sent_time_microsec + streamer->interval_microsec;
}

int timed_gaussian_streamer_init(timed_gaussian_streamer_t *streamer, uint64_t stream_id, const uint8_t *response, size_t total_response_size,
        size_t mean_chunk_size, size_t chunk_size_stddev, unsigned int seed, uint64_t time_interval_microsec) {
    streamer->interval_microsec = time_interval_microsec;
    streamer->_last_sent_time_microsec = 0; // we never sent anything
    printf("init streamer, total_response_size = %lu\n", total_response_size);
    return gaussian_streamer_init(&streamer->gaussian_streamer, stream_id, response, total_response_size, mean_chunk_size, chunk_size_stddev,
            seed);
}

int timed_gaussian_streamer_is_finished(timed_gaussian_streamer_t *streamer) {
    return gaussian_streamer_is_finished(&streamer->gaussian_streamer);
}



double compute_variance(const size_t *vals, size_t n_vals, size_t mean) {
    double sum = 0;
    for (int i = 0 ; i < n_vals ; i++) {
        sum += (((int64_t) vals[i]) - (int64_t) mean)*(((int64_t) vals[i]) - (int64_t) mean);
    }
    sum /= n_vals - 1;
    return sum;
}

/**
 * Here is a code that you can use to verify the distribution of the gaussian on your computer (as it depends on
 * rand_r(3), it might behave differently on different systems) :
 *
 * int main() {
 *     const int n_runs = 1000000;
 *     size_t results[n_runs];
 *     const size_t size = 10000000;
 *     unsigned int seed = 42;
 *     uint8_t *buffer = malloc(size);
 *     if (!buffer) {
 *             printf("malloc failed\n");
 *             return -1;
 *     }
 *     size_t mean_chunk_size = 10000;
 *     size_t std_dev = 1000;
 *     gaussian_streamer_t streamer;
 *     gaussian_streamer_init(&streamer, buffer, size, mean_chunk_size, std_dev, seed);
 *     size_t n_bytes = 0;
 *     size_t sum = 0;
 *     int i = 0;
 *     for (i = 0 ; i < n_runs && (n_bytes = gaussian_streamer_get_stream_nbytes(&streamer)) != 0 ; i++) {
 *             results[i] = n_bytes;
 *             sum += n_bytes;
 *     }
 *     printf("%d runs, mean = %lu bytes, std_dev = %f, total_delivered = %lu\n", i, sum/i, sqrt(compute_variance(results, i, sum/i)), sum);
 *     free(buffer);
 * }
 *
 *
 */



#endif //PICOQUIC_STREAMER_H
