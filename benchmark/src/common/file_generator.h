#ifndef BENCHMARK_FILE_GENERATOR_H
#define BENCHMARK_FILE_GENERATOR_H

#include <stddef.h>

#define LARGE_FILE_SIZE (630 * 1460)  /* 630 = sum of slow start packets up to 320, 1460 = MSS, works out to just over 9 MiB */
#define SHORT_FILE_SIZE (70 * 1460)          /* 70 packets, ~102 KB */

int generate_test_file(const char *filename, size_t size);

int verify_file_size(const char *filename, size_t expected_size);

#endif
