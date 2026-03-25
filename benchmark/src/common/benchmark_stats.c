#include "benchmark_stats.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Calculate throughput in Mbps from bytes and transfer time */
static double calculate_throughput_mbps(const transfer_stats_t* stats) {
    double duration_sec = timing_get_duration_ms(&stats->transfer_time) / 1000.0;
    if (duration_sec > 0) {
        return (stats->bytes_received * 8.0) / (duration_sec * 1000000.0);
    }
    return 0.0;
}

static char* get_chunk_array(const transfer_stats_t* stats) {
    char* result = NULL;
    char* tmp = NULL;

    if (asprintf(&result, "[") == -1) {
        return NULL;
    }

    for (size_t i = 0; i < stats->num_chunks; i++) {
        timing_t chunk_time = {0};
        chunk_time.start = stats->handshake_time.start;
        chunk_time.end = stats->chunk_stats[i].receive_time;
        chunk_time.valid = 1;
        double t_ms = timing_get_duration_ms(&chunk_time);

        if (i < stats->num_chunks - 1) {
            asprintf(&tmp, "%s{\"t_ms\":%.3f,\"bytes\":%zu},", result, t_ms, stats->chunk_stats[i].chunk_size);
        } else {
            asprintf(&tmp, "%s{\"t_ms\":%.3f,\"bytes\":%zu}", result, t_ms, stats->chunk_stats[i].chunk_size);
        }
        free(result);
        result = tmp;
        if (!result) {
            return NULL;
        }
    }

    if (asprintf(&tmp, "%s]", result) == -1) {
        free(result);
        return NULL;
    }
    free(result);
    return tmp;
}

char* get_json_stats(transfer_mode_t mode, const transfer_stats_t* large_file_stats,
                     const transfer_stats_t* small_file_stats) {
    const char* implementation = NULL;
    switch (mode) {
        case TRANSFER_MODE_TCP_NATIVE:
            implementation = "TCP native";
            break;
        case TRANSFER_MODE_PICOQUIC:
            implementation = "Picoquic";
            break;
        case TRANSFER_MODE_TAPS_TCP:
            implementation = "TAPS TCP";
            break;
        case TRANSFER_MODE_TAPS_QUIC:
            implementation = "TAPS QUIC";
            break;
        case TRANSFER_MODE_TAPS_RACING:
            implementation = "TAPS Racing";
            break;
    }

    double large_handshake_ms = timing_get_duration_ms(&large_file_stats->handshake_time);
    double large_transfer_ms = timing_get_duration_ms(&large_file_stats->transfer_time);
    double large_throughput = calculate_throughput_mbps(large_file_stats);

    double small_handshake_ms = 0;
    double small_transfer_ms = 0;
    double small_throughput = 0;
    size_t small_bytes = 0;

    if (small_file_stats) {
        small_handshake_ms = timing_get_duration_ms(&small_file_stats->handshake_time);
        small_transfer_ms = timing_get_duration_ms(&small_file_stats->transfer_time);
        small_throughput = calculate_throughput_mbps(small_file_stats);
        small_bytes = small_file_stats->bytes_received;
    }

    char* chunk_array = get_chunk_array(large_file_stats);

    char* json_str = NULL;
    int result;

    result = asprintf(&json_str,
                      "{\"implementation\": \"%s\","
                      "\"large_file\": {"
                      "\"handshake_time_ms\": %.2f,"
                      "\"transfer_time_ms\": %.2f,"
                      "\"bytes\": %zu,"
                      "\"throughput_mbps\": %.2f,"
                      "\"buckets\": %s"
                      "},"
                      "\"small_file\": {"
                      "\"handshake_time_ms\": %.2f,"
                      "\"transfer_time_ms\": %.2f,"
                      "\"bytes\": %zu,"
                      "\"throughput_mbps\": %.2f"
                      "}"
                      "}",
                      implementation,
                      large_handshake_ms,
                      large_transfer_ms,
                      large_file_stats->bytes_received,
                      large_throughput,
                      chunk_array,
                      small_handshake_ms,
                      small_transfer_ms,
                      small_bytes,
                      small_throughput
                      );

    if (chunk_array) {
        free(chunk_array);
    }

    if (result == -1) {
        return NULL;
    }

    return json_str;
}

transfer_stats_t* transfer_stats_new() {
    transfer_stats_t* stats = calloc(1, sizeof(transfer_stats_t));
    if (!stats) {
        return NULL;
    }
    stats->chunk_capacity = 1024;
    stats->chunk_stats = calloc(stats->chunk_capacity, sizeof(chunk_stats_t));
    if (!stats->chunk_stats) {
        free(stats);
        return NULL;
    }
    return stats;
}

chunk_stats_t* get_curr_chunk(transfer_stats_t* stats) {
    if (stats->num_chunks >= stats->chunk_capacity) {
        size_t new_capacity = stats->chunk_capacity * 2;
        chunk_stats_t* new_chunk_stats = realloc(stats->chunk_stats, new_capacity * sizeof(chunk_stats_t));
        if (!new_chunk_stats) {
            return NULL;
        }
        stats->chunk_stats = new_chunk_stats;
        stats->chunk_capacity = new_capacity;
    }
    return &stats->chunk_stats[stats->num_chunks];
}

void transfer_stats_free(transfer_stats_t* stats) {
    if (stats) {
        free(stats->chunk_stats);
        free(stats);
    }
}

void time_received_chunk(transfer_stats_t* stats, size_t bytes) {
    chunk_stats_t* curr_chunk = get_curr_chunk(stats);
    assert(curr_chunk);
    clock_gettime(CLOCK_MONOTONIC, &curr_chunk->receive_time);
    curr_chunk->chunk_size = bytes;

    stats->num_chunks++;
}
