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
    STREAM_STATE_IDLE,
    STREAM_STATE_SENDING_REQUEST,
    STREAM_STATE_RECEIVING,
    STREAM_STATE_DONE
} stream_state_t;

typedef struct {
    uint64_t stream_id;
    stream_state_t state;
    const char *request;
    size_t expected_size;
    transfer_stats_t stats;  /* Transfer statistics including handshake, transfer time, and bytes */
    int is_active;
} stream_ctx_t;

typedef struct {
    picoquic_cnx_t *cnx;
    stream_ctx_t large_stream;
    stream_ctx_t short_stream;
    uint64_t short_stream_start_time;
    timing_t handshake_time;  /* QUIC connection handshake time */
    int connection_established;
    int all_done;
} client_ctx_t;

static client_ctx_t client_ctx;

static void init_stream(stream_ctx_t *stream, const char *request, size_t expected_size) {
    memset(stream, 0, sizeof(stream_ctx_t));
    stream->request = request;
    stream->expected_size = expected_size;
    stream->state = STREAM_STATE_IDLE;
}

static void start_stream(stream_ctx_t *stream, uint64_t stream_id) {
    stream->stream_id = stream_id;
    stream->state = STREAM_STATE_SENDING_REQUEST;
    stream->is_active = 1;
    /* Transfer timing will start when request is sent */
}

static int client_callback(picoquic_cnx_t *cnx, uint64_t stream_id,
                          uint8_t *bytes, size_t length,
                          picoquic_call_back_event_t fin_or_event,
                          void *callback_ctx, void *stream_ctx) {
    
    client_ctx_t *ctx = (client_ctx_t *)callback_ctx;
    stream_ctx_t *s_ctx = NULL;
    int ret = 0;

    if (stream_id == ctx->large_stream.stream_id) {
        s_ctx = &ctx->large_stream;
    } else if (stream_id == ctx->short_stream.stream_id) {
        s_ctx = &ctx->short_stream;
    }

    switch (fin_or_event) {
    case picoquic_callback_ready:
        printf("Connection established\n");
        ctx->connection_established = 1;
        timing_end(&ctx->handshake_time);  /* End handshake timer */

        /* Assign handshake time to large stream, short stream will have 0 (reused connection) */
        ctx->large_stream.stats.handshake_time = ctx->handshake_time;

        uint64_t large_stream_id = picoquic_get_next_local_stream_id(cnx, 0);
        start_stream(&ctx->large_stream, large_stream_id);
        picoquic_mark_active_stream(cnx, large_stream_id, 1, NULL);
        break;

    case picoquic_callback_prepare_to_send:
        if (s_ctx && s_ctx->state == STREAM_STATE_SENDING_REQUEST) {
            size_t request_len = strlen(s_ctx->request);
            if (request_len <= length) {
                uint8_t* buf_ptr = picoquic_provide_stream_data_buffer(bytes, request_len, 1, 1);
                if (!buf_ptr) {
                    fprintf(stderr, "Error: Unable to get buffer for stream %llu\n",
                            (unsigned long long)stream_id);
                    return -1;
                }
                printf("[Stream %llu] Sending request: %s",
                       (unsigned long long)stream_id, s_ctx->request);
                memcpy(buf_ptr, s_ctx->request, request_len);
                s_ctx->state = STREAM_STATE_RECEIVING;
                timing_start(&s_ctx->stats.transfer_time);  /* Start transfer timer */
                return 0;
            }
        }
        break;

    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        if (s_ctx && s_ctx->state == STREAM_STATE_RECEIVING) {
            s_ctx->stats.bytes_received += length;

            if (fin_or_event == picoquic_callback_stream_fin) {
                timing_end(&s_ctx->stats.transfer_time);

                s_ctx->state = STREAM_STATE_DONE;
                s_ctx->is_active = 0;

                printf("[Stream %llu] Transfer complete (%zu bytes)\n",
                       (unsigned long long)stream_id, s_ctx->stats.bytes_received);

                /* If large stream just finished and we're waiting for short stream, start it now */
                if (s_ctx == &ctx->large_stream &&
                    ctx->short_stream.state == STREAM_STATE_IDLE &&
                    !ctx->short_stream.is_active) {
                    uint64_t now = timing_get_timestamp_us();
                    double elapsed_sec = 0;
                    if (ctx->short_stream_start_time > 0) {
                        elapsed_sec = (now - ctx->short_stream_start_time) / 1000000.0;
                    }
                    printf("\n--- Starting SHORT transfer (after LARGE complete, %.2fs elapsed) ---\n", elapsed_sec);
                    /* Set handshake time to valid zero (connection reused) */
                    clock_gettime(CLOCK_MONOTONIC, &ctx->short_stream.stats.handshake_time.start);
                    ctx->short_stream.stats.handshake_time.end = ctx->short_stream.stats.handshake_time.start;
                    ctx->short_stream.stats.handshake_time.valid = 1;
                    uint64_t short_stream_id = picoquic_get_next_local_stream_id(cnx, 0);
                    start_stream(&ctx->short_stream, short_stream_id);
                    picoquic_mark_active_stream(cnx, short_stream_id, 1, NULL);
                }

                if (ctx->large_stream.state == STREAM_STATE_DONE &&
                    ctx->short_stream.state == STREAM_STATE_DONE) {
                    printf("All transfers complete\n");
                    ctx->all_done = 1;
                    picoquic_close(cnx, 0);
                }
            }
        }
        break;

    case picoquic_callback_close:
    case picoquic_callback_application_close:
        printf("Connection closed\n");
        ctx->all_done = 1;
        break;

    default:
        break;
    }
    return ret;
}

static void print_stats(void) {
    stream_ctx_t *large = &client_ctx.large_stream;
    stream_ctx_t *short_s = &client_ctx.short_stream;

    /* Calculate throughput for console output */
    double large_transfer_ms = timing_get_duration_ms(&large->stats.transfer_time);
    double large_throughput = 0;
    if (large_transfer_ms > 0) {
        large_throughput = (large->stats.bytes_received * 8.0) / (large_transfer_ms / 1000.0) / 1000000.0;
    }

    double short_transfer_ms = timing_get_duration_ms(&short_s->stats.transfer_time);
    double short_throughput = 0;
    if (short_transfer_ms > 0) {
        short_throughput = (short_s->stats.bytes_received * 8.0) / (short_transfer_ms / 1000.0) / 1000000.0;
    }

    printf("\n=== LARGE File Transfer Stats ===\n");
    printf("Handshake time: %.2f ms\n", timing_get_duration_ms(&large->stats.handshake_time));
    printf("Transfer time: %.2f ms\n", large_transfer_ms);
    printf("Bytes received: %zu\n", large->stats.bytes_received);
    printf("Throughput: %.2f Mbps\n", large_throughput);

    printf("\n=== SHORT File Transfer Stats ===\n");
    printf("Handshake time: %.2f ms (reused connection)\n", timing_get_duration_ms(&short_s->stats.handshake_time));
    printf("Transfer time: %.2f ms\n", short_transfer_ms);
    printf("Bytes received: %zu\n", short_s->stats.bytes_received);
    printf("Throughput: %.2f Mbps\n", short_throughput);

    char *json = get_json_stats(TRANSFER_MODE_PICOQUIC, &large->stats, &short_s->stats, 1);
    if (json) {
        printf("\n%s\n", json);
        free(json);
    }
}

static int sample_client_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, 
    void* callback_ctx, void * callback_arg)
{
    int ret = 0;
    client_ctx_t* cb_ctx = (client_ctx_t*)callback_ctx;

    if (cb_ctx == NULL) {
        ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    else {
        switch (cb_mode) {
        case picoquic_packet_loop_ready:
            fprintf(stdout, "Waiting for packets.\n");
            break;
        case picoquic_packet_loop_after_receive:
            break;
        case picoquic_packet_loop_after_send:
            if (cb_ctx->all_done) {
                ret = PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }
            break;
        case picoquic_packet_loop_port_update:
            break;
        default:
            ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
            break;
        }
    }
    return ret;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 4433;
    int ret = 0;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = atoi(argv[2]);
    }

    printf("QUIC Client connecting to %s:%d\n", host, port);

    memset(&client_ctx, 0, sizeof(client_ctx));

    init_stream(&client_ctx.large_stream, REQUEST_LARGE, LARGE_FILE_SIZE);
    init_stream(&client_ctx.short_stream, REQUEST_SHORT, SHORT_FILE_SIZE);

    picoquic_quic_t *quic = picoquic_create(1, NULL, NULL, NULL, ALPN,
                                            NULL, NULL, NULL, NULL, NULL,
                                            picoquic_current_time(), NULL, NULL, NULL, 0);
    picoquic_set_textlog(quic, "client_debug.log");
    if (!quic) {
        fprintf(stderr, "Failed to create QUIC context\n");
        return 1;
    }

    picoquic_set_default_congestion_algorithm(quic, picoquic_bbr_algorithm);

    struct sockaddr_storage server_addr;
    int is_name = 0;
    ret = picoquic_get_server_address(host, port, &server_addr, &is_name);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve server address\n");
        picoquic_free(quic);
        return 1;
    }

    printf("\n--- Transferring LARGE file via QUIC ---\n");
    timing_start(&client_ctx.handshake_time);  /* Start handshake timer */

    client_ctx.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
                                        picoquic_null_connection_id,
                                        (struct sockaddr *)&server_addr,
                                        picoquic_current_time(),
                                        0, host, ALPN, 1);

    if (!client_ctx.cnx) {
        fprintf(stderr, "Failed to create connection\n");
        picoquic_free(quic);
        return 1;
    }

    picoquic_set_callback(client_ctx.cnx, client_callback, &client_ctx);

    ret = picoquic_start_client_cnx(client_ctx.cnx);
    if (ret != 0) {
        fprintf(stderr, "Failed to start connection\n");
        picoquic_free(quic);
        return 1;
    }

    ret = picoquic_packet_loop(quic, 0, server_addr.ss_family, 0, 0, 0, sample_client_loop_cb, &client_ctx);

    if (client_ctx.all_done) {
        print_stats();
    } else {
        fprintf(stderr, "Transfer did not complete successfully\n");
        ret = 1;
    }

    if (quic) {
        picoquic_free(quic);
    }

    printf("Client exiting with code %d\n", ret);

    return ret;
}
