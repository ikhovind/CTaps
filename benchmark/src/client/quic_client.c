#include "../common/protocol.h"
#include "../common/timing.h"
#include "../common/file_generator.h"
#include "../common/benchmark_stats.h"
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <picoquic_packet_loop.h>
#include <picoquic_bbr.h>
#include <picoquic_set_textlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALPN "benchmark"

typedef enum {
    STREAM_STATE_NOT_STARTED,
    STREAM_STATE_RECEIVING,
    STREAM_STATE_DONE
} stream_state_t;

typedef struct {
    stream_state_t state;
    const char *request;
    size_t expected_size;
    transfer_stats_t stats;  /* Transfer statistics including handshake, transfer time, and bytes */
} stream_ctx_t;

typedef struct {
    picoquic_cnx_t *cnx;
    stream_ctx_t large_stream;
    stream_ctx_t short_stream;
    uint64_t short_stream_start_time;
    int all_done;
} client_ctx_t;

static int json_only_mode = 0;

static void init_stream(stream_ctx_t *stream, const char *request, size_t expected_size) {
    memset(stream, 0, sizeof(stream_ctx_t));
    stream->request = request;
    stream->expected_size = expected_size;
    stream->state = STREAM_STATE_NOT_STARTED;
}

static void start_stream(client_ctx_t* client_ctx, stream_ctx_t *stream) {
    timing_end(&stream->stats.handshake_time);  /* End handshake timer */
    stream->state = STREAM_STATE_RECEIVING;
    clock_gettime(CLOCK_MONOTONIC, &stream->stats.transfer_time.start);
    int stream_id = picoquic_get_next_local_stream_id(client_ctx->cnx, 0);
    picoquic_add_to_stream_with_ctx(client_ctx->cnx, stream_id, stream->request, strlen(stream->request), 1, stream);
}

static int client_callback(picoquic_cnx_t *cnx, uint64_t stream_id,
                          uint8_t *bytes, size_t length,
                          picoquic_call_back_event_t fin_or_event,
                          void *callback_ctx, void *stream_ctx) {

    client_ctx_t *ctx = (client_ctx_t *)callback_ctx;
    stream_ctx_t *s_ctx = (stream_ctx_t *)stream_ctx;

    switch (fin_or_event) {
    case picoquic_callback_ready:
        if (!json_only_mode) printf("Connection established\n");
        start_stream(ctx, &ctx->large_stream);
        break;

    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        s_ctx->stats.bytes_received += length;
        if (fin_or_event == picoquic_callback_stream_fin) {
            timing_end(&s_ctx->stats.transfer_time);
            s_ctx->state = STREAM_STATE_DONE;

            if (!json_only_mode) {
                printf("[Stream %llu] Transfer complete (%zu bytes)\n",
                       (unsigned long long)stream_id, s_ctx->stats.bytes_received);
            }

            if (s_ctx == &ctx->large_stream && ctx->short_stream.state == STREAM_STATE_NOT_STARTED) {
                if (!json_only_mode) {
                    printf("\n--- Starting SHORT transfer ---\n");
                }
                timing_start(&ctx->short_stream.stats.handshake_time);
                start_stream(ctx, &ctx->short_stream);
            }

            if (ctx->large_stream.state == STREAM_STATE_DONE &&
                ctx->short_stream.state == STREAM_STATE_DONE) {
                if (!json_only_mode) printf("All transfers complete\n");
                picoquic_close(cnx, 0);
            }
        }
        break;

    case picoquic_callback_close:
    case picoquic_callback_application_close:
        if (!json_only_mode) printf("Connection closed\n");
        // This flag is checked in the loop cb to terminate the loop
        ctx->all_done = 1;
        break;

    default:
        break;
    }
    return 0;
}

static int sample_client_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
    void* callback_ctx, void * callback_arg) {
    if (callback_ctx == NULL) {
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    client_ctx_t* cb_ctx = (client_ctx_t*)callback_ctx;

    if (cb_mode == picoquic_packet_loop_after_send && cb_ctx->all_done) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 4433;
    int ret = 0;
    int arg_idx = 1;

    if (argc > arg_idx) {
        host = argv[arg_idx];
        arg_idx++;
    }
    if (argc > arg_idx) {
        port = atoi(argv[arg_idx]);
        arg_idx++;
    }

    if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
        json_only_mode = 1;
        arg_idx++;
    }

    if (!json_only_mode) printf("QUIC Client connecting to %s:%d\n", host, port);


    client_ctx_t client_ctx;

    memset(&client_ctx, 0, sizeof(client_ctx));

    init_stream(&client_ctx.large_stream, REQUEST_LARGE, LARGE_FILE_SIZE);
    init_stream(&client_ctx.short_stream, REQUEST_SHORT, SHORT_FILE_SIZE);

    picoquic_quic_t *quic = picoquic_create(1, NULL, NULL, NULL, ALPN,
                                            NULL, NULL, NULL, NULL, NULL,
                                            picoquic_current_time(), NULL, NULL, NULL, 0);
    if (!quic) {
        fprintf(stderr, "ERROR: Failed to create QUIC context\n");
        return -1;
    }

    struct sockaddr_storage server_addr;
    int is_name = 0;
    ret = picoquic_get_server_address(host, port, &server_addr, &is_name);
    if (ret != 0) {
        fprintf(stderr, "ERROR: Failed to resolve server address\n");
        picoquic_free(quic);
        return -1;
    }

    if (!json_only_mode) printf("\n--- Transferring LARGE file via QUIC ---\n");
    timing_start(&client_ctx.large_stream.stats.handshake_time);  /* Start handshake timer */

    client_ctx.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
                                        picoquic_null_connection_id,
                                        (struct sockaddr *)&server_addr,
                                        picoquic_current_time(),
                                        0, host, ALPN, 1);

    if (!client_ctx.cnx) {
        fprintf(stderr, "ERROR: Failed to create connection\n");
        picoquic_free(quic);
        return -1;
    }

    picoquic_set_callback(client_ctx.cnx, client_callback, &client_ctx);

    ret = picoquic_start_client_cnx(client_ctx.cnx);
    if (ret != 0) {
        fprintf(stderr, "ERROR: Failed to start connection\n");
        picoquic_free(quic);
        return -1;
    }

    ret = picoquic_packet_loop(quic, 0, server_addr.ss_family, 0, 0, 0, sample_client_loop_cb, &client_ctx);

    if (client_ctx.all_done) {
        char *json = get_json_stats(TRANSFER_MODE_PICOQUIC, &client_ctx.large_stream.stats, &client_ctx.short_stream.stats, 1);
        if (json) {
            printf("%s\n", json);
            free(json);
        }
    } else {
        fprintf(stderr, "ERROR: Transfer did not complete successfully\n");
        ret = 1;
    }

    if (quic) {
        picoquic_free(quic);
    }

    if (!json_only_mode) printf("Client exiting with code %d\n", ret);

    return ret;
}
