#ifndef BENCHMARK_STATS_H
#define BENCHMARK_STATS_H

#include <stddef.h>
#include "timing.h"
#include "protocol.h"

typedef struct {
    struct timespec receive_time;
    size_t chunk_size;
} chunk_stats_t;

typedef struct {
    timing_t handshake_time;
    timing_t transfer_time;
    size_t bytes_received;
    chunk_stats_t* chunk_stats;
    size_t num_chunks;
    size_t chunk_capacity; // Number of chunk_stats allocated
} transfer_stats_t;

/*
* Returns an allocated JSON string containing the benchmark statistics.
*/
char* get_json_stats(transfer_mode_t mode, const transfer_stats_t* large_file_stats,
                     const transfer_stats_t* small_file_stats);

void transfer_stats_free(transfer_stats_t* stats);

void time_received_chunk(transfer_stats_t* stats, size_t bytes);

transfer_stats_t* transfer_stats_new();
#endif
