I #include "../common/protocol.h"
I #include "../common/timing.h"
I #include "../common/file_generator.h"
I #include "../common/benchmark_stats.h"
-
I #include <picoquic_packet_loop.h>
I #include <picoquic.h>
I #include <stdio.h>
I #include <stdlib.h>
I #include <string.h>
I #include <unistd.h>
I #include <arpa/inet.h>
-
I #define ALPN "benchmark"
-
-
I typedef enum { STREAM_STATE_NOT_STARTED, STREAM_STATE_RECEIVING, STREAM_STATE_DONE } stream_state_enum_t;
-
I typedef struct {
I     stream_state_enum_t state;
I     const char* request;
I     size_t expected_size;
I     transfer_stats_t* stats; /* Transfer statistics including handshake, transfer time, and bytes */
I } stream_ctx_t;
-
I typedef struct {
I     picoquic_cnx_t* cnx;
I     stream_ctx_t large_stream;
I     stream_ctx_t short_stream;
I     int all_done;
M     struct sockaddr_storage server_addr;
M     struct sockaddr_storage new_local_addr;
I } client_ctx_t;
-
I int json_only_mode = 0;
-
I static void init_stream(stream_ctx_t* stream, const char* request, size_t expected_size) {
I     memset(stream, 0, sizeof(stream_ctx_t));
I     stream->request = request;
I     stream->expected_size = expected_size;
I     stream->state = STREAM_STATE_NOT_STARTED;
I     stream->stats = transfer_stats_new();
I }
-
C static void start_stream(client_ctx_t* client_ctx, stream_ctx_t* stream) {
C     timing_end(&stream->stats->handshake_time); /* End handshake timer */
C     stream->state = STREAM_STATE_RECEIVING;
C     clock_gettime(CLOCK_MONOTONIC, &stream->stats->transfer_time.start);
C     int stream_id = picoquic_get_next_local_stream_id(client_ctx->cnx, 0);
D     picoquic_add_to_stream_with_ctx(client_ctx->cnx, stream_id, (const uint8_t*)stream->request,
D                                     strlen(stream->request), 1, stream);
C }
-
C static int client_callback(picoquic_cnx_t* cnx, uint64_t stream_id, uint8_t* bytes, size_t length,
C                            picoquic_call_back_event_t fin_or_event, void* callback_ctx,
C                            void* stream_ctx) {
-
-     (void)bytes;
I     client_ctx_t* ctx = (client_ctx_t*)callback_ctx;
I     stream_ctx_t* s_ctx = (stream_ctx_t*)stream_ctx;
-
C     switch (fin_or_event) {
C     case picoquic_callback_ready:
C         if (!json_only_mode) {
C             printf("Connection established\n");
C         }
C         start_stream(ctx, &ctx->large_stream);
C         break;
-
M     case picoquic_callback_path_suspended:
M         if (!json_only_mode) {
M             printf("Path suspended (path_id=%llu)\n", (unsigned long long)stream_id);
M         }
-
M         if (picoquic_probe_new_path_ex(cnx,
M                 (const struct sockaddr*)&ctx->server_addr,
M                 (const struct sockaddr*)&ctx->new_local_addr, 0,
M                 picoquic_current_time(), 1) != 0) {
E             if (!json_only_mode) {
E                 fprintf(stderr, "WARNING: Failed to probe new path for migration\n");
E             }
M         } else {
M             if (!json_only_mode) {
M                 printf("Probing new path from 127.0.0.2 for connection migration\n");
M             }
M         }
M         break;
-
D     case picoquic_callback_stream_data:
D     case picoquic_callback_stream_fin:
D         s_ctx->stats->bytes_received += length;
D         time_received_chunk(s_ctx->stats, length);
D         if (fin_or_event == picoquic_callback_stream_fin) {
D             timing_end(&s_ctx->stats->transfer_time);
D             s_ctx->state = STREAM_STATE_DONE;
-
D             if (!json_only_mode) {
D                 printf("[Stream %llu] Transfer complete (%zu bytes)\n",
D                        (unsigned long long)stream_id, s_ctx->stats->bytes_received);
D             }
-
D             if (s_ctx == &ctx->large_stream &&
D                 ctx->short_stream.state == STREAM_STATE_NOT_STARTED) {
D                 if (!json_only_mode) {
D                     printf("\n--- Starting SHORT transfer ---\n");
D                 }
C                 timing_start(&ctx->short_stream.stats->handshake_time);
C                 start_stream(ctx, &ctx->short_stream);
D             }
-
D             if (ctx->large_stream.state == STREAM_STATE_DONE &&
D                 ctx->short_stream.state == STREAM_STATE_DONE) {
D                 if (!json_only_mode) {
D                     printf("All transfers complete\n");
D                 }
T                 picoquic_close(cnx, 0);
T             }
D         }
D         break;
-
T     case picoquic_callback_close:
T     case picoquic_callback_application_close:
T         if (!json_only_mode) {
T             printf("Connection closed\n");
T         }
T         /* This flag is checked in the loop cb to terminate the loop */
T         ctx->all_done = 1;
T         break;
-
I     default:
I         break;
I     }
I     return 0;
I }
-
C static int sample_client_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
C                                  void* callback_ctx, void* callback_arg) {
-     (void)quic;
-     (void)callback_arg;
E     if (!callback_ctx) {
E         return PICOQUIC_ERROR_UNEXPECTED_ERROR;
E     }
C     client_ctx_t* cb_ctx = (client_ctx_t*)callback_ctx;
-
T     if (cb_mode == picoquic_packet_loop_after_send && cb_ctx->all_done) {
T         return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
T     }
C     return 0;
C }
-
I int main(int argc, char* argv[]) {
I     const char* host = "127.0.0.1";
I     int port = DEFAULT_PORT;
I     int ret = 0;
I     int arg_idx = 1;
-
I     if (argc > arg_idx) {
I         host = argv[arg_idx];
I         arg_idx++;
I     }
I     if (argc > arg_idx) {
I         port = atoi(argv[arg_idx]);
I         arg_idx++;
I     }
-
I     if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
I         json_only_mode = 1;
I         arg_idx++;
I     }
-
I     if (!json_only_mode) {
I         printf("QUIC Client connecting to %s:%d\n", host, port);
I     }
-
I     client_ctx_t client_ctx;
I     memset(&client_ctx, 0, sizeof(client_ctx));
-
I     init_stream(&client_ctx.large_stream, REQUEST_LARGE, LARGE_FILE_SIZE);
I     init_stream(&client_ctx.short_stream, REQUEST_SHORT, SHORT_FILE_SIZE);
-
-     /* Resolve and store server address in context (needed by migration probe in callback) */
M     int is_name = 0;
M     ret = picoquic_get_server_address(host, port, &client_ctx.server_addr, &is_name);
E     if (ret != 0) {
E         fprintf(stderr, "ERROR: Failed to resolve server address\n");
E         return -1;
E     }
-
C     /* Set up the migration source address: 127.0.0.2, ephemeral port */
M     struct sockaddr_in* new_local = (struct sockaddr_in*)&client_ctx.new_local_addr;
M     memset(new_local, 0, sizeof(*new_local));
M     new_local->sin_family = AF_INET;
M     new_local->sin_addr.s_addr = inet_addr("127.0.0.2");
M     new_local->sin_port = 0; /* OS picks ephemeral port */
-
I     picoquic_quic_t* quic = picoquic_create(1, NULL, NULL, NULL, ALPN, NULL, NULL, NULL, NULL, NULL,
I                                             picoquic_current_time(), NULL, NULL, NULL, 0);
E     if (!quic) {
E         fprintf(stderr, "ERROR: Failed to create QUIC context\n");
E         return -1;
E     }
-
D     if (!json_only_mode) {
D         printf("\n--- Transferring LARGE file via QUIC ---\n");
D     }
C     timing_start(&client_ctx.large_stream.stats->handshake_time); /* Start handshake timer */
-
M     picoquic_enable_path_callbacks_default(quic, 1);
-
C     client_ctx.cnx = picoquic_create_cnx(
C         quic, picoquic_null_connection_id, picoquic_null_connection_id,
C         (const struct sockaddr*)&client_ctx.server_addr, picoquic_current_time(), 0, host, ALPN, 1);
-
E     if (!client_ctx.cnx) {
E         fprintf(stderr, "ERROR: Failed to create connection\n");
T         picoquic_free(quic);
E         return -1;
E     }
-
C     picoquic_set_callback(client_ctx.cnx, client_callback, &client_ctx);
-
-
C     ret = picoquic_start_client_cnx(client_ctx.cnx);
E     if (ret != 0) {
E         fprintf(stderr, "ERROR: Failed to start connection\n");
T         picoquic_free(quic);
E         return -1;
E     }
-
C     /*
C      * Bind to INADDR_ANY (0.0.0.0) so the packet loop socket can send and
C      * receive on both 127.0.0.1 and 127.0.0.2. Passing 0 as the local port
C      * lets the OS assign an ephemeral port.
C      */
C     ret = picoquic_packet_loop(quic, 0, AF_INET, 0, 0, 0, sample_client_loop_cb,
C                                &client_ctx);
-
T     if (client_ctx.all_done) {
T         char* json = get_json_stats(TRANSFER_MODE_PICOQUIC, client_ctx.large_stream.stats,
T                                     client_ctx.short_stream.stats);
T         if (json) {
T             printf("%s\n", json);
T             free(json);
T         }
E     } else {
E         fprintf(stderr, "ERROR: Transfer did not complete successfully\n");
E         ret = 1;
E     }
-
T     if (quic) {
T         picoquic_free(quic);
T     }
-
I     if (!json_only_mode) {
I         printf("Client exiting with code %d\n", ret);
I     }
-
T     return ret;
T }
