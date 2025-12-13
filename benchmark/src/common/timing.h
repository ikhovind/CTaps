#ifndef BENCHMARK_TIMING_H
#define BENCHMARK_TIMING_H

#include <stdint.h>
#include <time.h>

typedef struct {
    struct timespec start;
    struct timespec end;
    int valid;
} timing_t;

void timing_start(timing_t *timing);

void timing_end(timing_t *timing);

double timing_get_duration_ms(const timing_t *timing);

double timing_get_duration_us(const timing_t *timing);

uint64_t timing_get_timestamp_us(void);

#endif
