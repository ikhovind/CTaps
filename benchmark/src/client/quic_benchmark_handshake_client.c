#include "../common/protocol.h"
#include "../common/timing.h"
#include "../common/file_generator.h"
#include "../common/benchmark_stats.h"

#include <picoquic_packet_loop.h>
#include <picoquic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define ALPN "benchmark"

typedef struct {
    picoquic_cnx_t* cnx;
    int all_done;
    struct sockaddr_storage server_addr;
    transfer_stats_t* stats;
} client_ctx_t;

int json_only_mode = 0;

static int client_callback(picoquic_cnx_t* cnx, uint64_t stream_id, uint8_t* bytes, size_t length,
                           picoquic_call_back_event_t fin_or_event, void* callback_ctx,
                           void* stream_ctx) {
    (void)stream_id;
    (void)bytes;
    (void)length;
    (void)stream_ctx;

    client_ctx_t* ctx = (client_ctx_t*)callback_ctx;

    switch (fin_or_event) {
    case picoquic_callback_ready:
        if (!json_only_mode) {
            printf("Connection established\n");
        }
        timing_end(&ctx->stats->handshake_time);
        ctx->all_done = 1;
        picoquic_close(cnx, 0);
        break;
    default:
        break;
    }
    return 0;
}

static int sample_client_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
                                 void* callback_ctx, void* callback_arg) {
    (void)callback_arg;
    (void)quic;
    if (!callback_ctx) {
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    client_ctx_t* cb_ctx = (client_ctx_t*)callback_ctx;

    if (cb_mode == picoquic_packet_loop_after_send && cb_ctx->all_done) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    int port = DEFAULT_PORT;
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

    if (!json_only_mode) {
        printf("QUIC Client connecting to %s:%d\n", host, port);
    }

    struct sockaddr_storage server_addr;

    int is_name = 0;
    ret = picoquic_get_server_address(host, port, &server_addr, &is_name);
    if (ret != 0) {
        fprintf(stderr, "ERROR: Failed to resolve server address\n");
        return -1;
    }

    client_ctx_t client_ctx;
    memset(&client_ctx, 0, sizeof(client_ctx));

    /* Set up the migration source address: 127.0.0.2, ephemeral port */
    picoquic_quic_t* quic = picoquic_create(1, NULL, NULL, NULL, ALPN, NULL, NULL, NULL, NULL, NULL,
                                            picoquic_current_time(), NULL, NULL, NULL, 0);
    if (!quic) {
        fprintf(stderr, "ERROR: Failed to create QUIC context\n");
        return -1;
    }

    if (!json_only_mode) {
        printf("\n--- Transferring LARGE file via QUIC ---\n");
    }

    client_ctx.stats = transfer_stats_new();
    timing_start(&client_ctx.stats->handshake_time); /* Start handshake timer */

    client_ctx.cnx = picoquic_create_cnx(
        quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (const struct sockaddr*)&server_addr, picoquic_current_time(), 0, host, ALPN, 1);

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

    /*
     * Bind to INADDR_ANY (0.0.0.0) so the packet loop socket can send and
     * receive on both 127.0.0.1 and 127.0.0.2. Passing 0 as the local port
     * lets the OS assign an ephemeral port.
     */
    ret = picoquic_packet_loop(quic, 0, AF_INET, 0, 0, 0, sample_client_loop_cb,
                               &client_ctx);

    if (client_ctx.all_done) {
        char* json = get_json_stats(TRANSFER_MODE_PICOQUIC, client_ctx.stats, NULL);
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

    if (!json_only_mode) {
        printf("Client exiting with code %d\n", ret);
    }

    return ret;
}
