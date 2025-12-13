#include "timing.h"
#include <stdio.h>
#include <string.h>

void timing_start(timing_t *timing) {
    memset(timing, 0, sizeof(timing_t));
    clock_gettime(CLOCK_MONOTONIC, &timing->start);
    timing->valid = 0;
}

void timing_end(timing_t *timing) {
    clock_gettime(CLOCK_MONOTONIC, &timing->end);
    timing->valid = 1;
}

double timing_get_duration_ms(const timing_t *timing) {
    if (!timing->valid) {
        return -1.0;
    }

    double start_ms = timing->start.tv_sec * 1000.0 + timing->start.tv_nsec / 1000000.0;
    double end_ms = timing->end.tv_sec * 1000.0 + timing->end.tv_nsec / 1000000.0;

    return end_ms - start_ms;
}

double timing_get_duration_us(const timing_t *timing) {
    if (!timing->valid) {
        return -1.0;
    }

    double start_us = timing->start.tv_sec * 1000000.0 + timing->start.tv_nsec / 1000.0;
    double end_us = timing->end.tv_sec * 1000000.0 + timing->end.tv_nsec / 1000.0;

    return end_us - start_us;
}

uint64_t timing_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}
