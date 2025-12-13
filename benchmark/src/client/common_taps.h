#include "../common/protocol.h"
#include "../common/timing.h"
#include "../common/file_generator.h"
#include "../common/benchmark_stats.h"
#include "ctaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

typedef enum {
    TRANSFER_NONE_STARTED,
    STATE_LARGE_STARTED,
    STATE_LARGE_DONE,
    STATE_SHORT_STARTED,
    STATE_BOTH_DONE,
} transfer_progress_t;

typedef struct {
    const char *host;
    int port;

    transfer_progress_t state;

    transfer_stats_t large_stats;
    transfer_stats_t short_stats;
    int transfer_complete;
} client_context_t;

extern client_context_t client_ctx;

int on_connection_ready(ct_connection_t *connection);

int on_establishment_error(ct_connection_t* connection);
