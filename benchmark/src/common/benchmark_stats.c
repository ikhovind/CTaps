#define _GNU_SOURCE
#include "benchmark_stats.h"
#include <stdio.h>
#include <stdlib.h>

/* Calculate throughput in Mbps from bytes and transfer time */
static double calculate_throughput_mbps(const transfer_stats_t *stats) {
    double duration_sec = timing_get_duration_ms(&stats->transfer_time) / 1000.0;
    if (duration_sec > 0) {
        return (stats->bytes_received * 8.0) / (duration_sec * 1000000.0);
    }
    return 0.0;
}

char* get_json_stats(transfer_mode_t mode,
                     const transfer_stats_t *large_file_stats,
                     const transfer_stats_t *small_file_stats,
                     int multi_streaming) {
    const char *implementation = (mode == TRANSFER_MODE_TCP_NATIVE) ? "tcp_native" : "quic_native";

    double large_handshake_ms = timing_get_duration_ms(&large_file_stats->handshake_time);
    double large_transfer_ms = timing_get_duration_ms(&large_file_stats->transfer_time);
    double large_throughput = calculate_throughput_mbps(large_file_stats);

    double small_handshake_ms = timing_get_duration_ms(&small_file_stats->handshake_time);
    double small_transfer_ms = timing_get_duration_ms(&small_file_stats->transfer_time);
    double small_throughput = calculate_throughput_mbps(small_file_stats);

    char *json_str = NULL;
    int result;

    if (multi_streaming) {
        result = asprintf(&json_str,
            "{\"implementation\": \"%s\","
            "\"large_file\": {"
            "\"handshake_time_ms\": %.2f,"
            "\"transfer_time_ms\": %.2f,"
            "\"bytes\": %zu,"
            "\"throughput_mbps\": %.2f"
            "},"
            "\"small_file\": {"
            "\"handshake_time_ms\": %.2f,"
            "\"transfer_time_ms\": %.2f,"
            "\"bytes\": %zu,"
            "\"throughput_mbps\": %.2f"
            "},"
            "\"multi_streaming\": true}",
            implementation,
            large_handshake_ms, large_transfer_ms, large_file_stats->bytes_received, large_throughput,
            small_handshake_ms, small_transfer_ms, small_file_stats->bytes_received, small_throughput);
    } else {
        result = asprintf(&json_str,
            "{\"implementation\": \"%s\","
            "\"large_file\": {"
            "\"handshake_time_ms\": %.2f,"
            "\"transfer_time_ms\": %.2f,"
            "\"bytes\": %zu,"
            "\"throughput_mbps\": %.2f"
            "},"
            "\"small_file\": {"
            "\"handshake_time_ms\": %.2f,"
            "\"transfer_time_ms\": %.2f,"
            "\"bytes\": %zu,"
            "\"throughput_mbps\": %.2f"
            "}}",
            implementation,
            large_handshake_ms, large_transfer_ms, large_file_stats->bytes_received, large_throughput,
            small_handshake_ms, small_transfer_ms, small_file_stats->bytes_received, small_throughput);
    }

    if (result == -1) {
        return NULL;
    }

    return json_str;
}




