#ifndef BENCHMARK_PROTOCOL_H
#define BENCHMARK_PROTOCOL_H

#define DEFAULT_PORT 8888
#define BUFFER_SIZE 65536

typedef enum {
    FILE_TYPE_LARGE = 0,
    FILE_TYPE_SHORT = 1
} file_type_t;

typedef enum {
    TRANSFER_MODE_TCP_NATIVE = 0,
    TRANSFER_MODE_PICOQUIC,
    TRANSFER_MODE_TAPS
} transfer_mode_t;

#define REQUEST_LARGE "LARGE"
#define REQUEST_SHORT "SHORT"

#endif
