#ifndef BENCHMARK_STATS_H
#define BENCHMARK_STATS_H

#include <stddef.h>
#include "timing.h"
#include "protocol.h"

typedef struct {
    timing_t handshake_time;
    timing_t transfer_time;
    size_t bytes_received;
} transfer_stats_t;

/*
* Returns an allocated JSON string containing the benchmark statistics.
*/
char* get_json_stats(transfer_mode_t mode,
                     const transfer_stats_t *large_file_stats,
                     const transfer_stats_t *small_file_stats,
                     int multi_streaming);
#endif
