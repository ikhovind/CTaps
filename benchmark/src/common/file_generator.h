#ifndef BENCHMARK_FILE_GENERATOR_H
#define BENCHMARK_FILE_GENERATOR_H

#include <stddef.h>

// 4.6 MiB. This gives us a good transfer time to show
// connection migration for 5 mbps migration benchmarking, while
// it is more than large enough for CWND to reach > 70 
// For our large file/small file benchmarking which uses 50 mbps.
// If we set the shortest RTT to be 50 ms in our benchmark, the steady-state CWND for 50 mbps is:
// 50/8 * 10 ^6 bandwith in bytes * (50/1000 ms RTT)/ (1460 packet size) = 214 packets
// To reach >70 We need the slow start flow of: 10 + 20 + 40 + 80 -> Which transfers
// in total 150 * 1460, far less than the 3150 * 1460 we have here, so we are guaranteed to reach >70 CWND.
// Since it doesn't matter if we are far above 70 CWND, we just use a large file to have a single one
// for all benchmarks.
#define LARGE_FILE_SIZE                                                                            \
    (3150 *                                                                                         \
     1460)
#define SHORT_FILE_SIZE (70 * 1460) /* 70 packets, ~102 KB */

int generate_test_file(const char* filename, size_t size);

int verify_file_size(const char* filename, size_t expected_size);

#endif
