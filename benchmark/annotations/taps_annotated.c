I #include "ctaps.h"
I #include "../common/protocol.h"
I #include "../common/timing.h"
I #include "../common/file_generator.h"
I #include "../common/benchmark_stats.h"
I #include <stdio.h>
I #include <stdlib.h>
I #include <string.h>
I #include <unistd.h>
I #include <arpa/inet.h>
-
I typedef enum {
I     TRANSFER_NONE_STARTED,
I     STATE_LARGE_STARTED,
I     STATE_LARGE_DONE,
I     STATE_SHORT_STARTED,
I     STATE_BOTH_DONE,
I } transfer_progress_t;
-
I typedef struct {
I     const char* host;
I     int port;
-
I     transfer_progress_t state;
-
I     transfer_stats_t* large_stats;
I     transfer_stats_t* short_stats;
I     int transfer_complete;
I } client_context_t;
-
-
I int json_only_mode = 0;
-
C void initiate_short_transfer(ct_connection_t* large_file_connection) {
C     client_context_t* client_ctx = ct_connection_get_callback_context(large_file_connection);
C     timing_start(&client_ctx->short_stats->handshake_time);
C     int clone_res = ct_connection_clone(large_file_connection);
E     if (clone_res < 0) {
E         if (!json_only_mode) {
E             printf("Error: Failed to clone connection for SHORT file transfer\n");
E         }
E         return;
E     }
C }
-
D void on_msg_received(ct_connection_t* connection, ct_message_t* msg,
D                     ct_message_context_t* ctx) {
D     unsigned int msg_length = ct_message_get_length(msg);
D     client_context_t* client_ctx = ct_connection_get_callback_context(connection);
-
D     switch (client_ctx->state) {
D     case STATE_LARGE_STARTED:
-
D         client_ctx->large_stats->bytes_received += msg_length;
D         time_received_chunk(client_ctx->large_stats, msg_length);
D         if (client_ctx->large_stats->bytes_received >= LARGE_FILE_SIZE) {
D             if (!json_only_mode) {
D                 printf("LARGE file transfers of size %zu completed.\n", LARGE_FILE_SIZE);
D             }
D             timing_end(&client_ctx->large_stats->transfer_time);
D             client_ctx->state = STATE_LARGE_DONE;
C             initiate_short_transfer(connection);
D         } else {
D                 ct_receive_callbacks_t receive_callbacks = {
D                     .receive_callback = on_msg_received,
D                     .per_receive_context = ctx
D                 };
D             ct_receive_message(connection, &receive_callbacks);
D         }
D         break;
D     case STATE_SHORT_STARTED:
D         client_ctx->short_stats->bytes_received += msg_length;
D         if (client_ctx->short_stats->bytes_received >= SHORT_FILE_SIZE) {
D             timing_end(&client_ctx->short_stats->transfer_time);
D             client_ctx->state = STATE_BOTH_DONE;
D             client_ctx->transfer_complete = 1;
D             if (!json_only_mode) {
D                 printf("Both LARGE and SHORT file transfers completed successfully.\n");
D          }
T             ct_connection_close_group(connection);
D         } else {
D             ct_receive_callbacks_t receive_callbacks = {
D                 .receive_callback = on_msg_received,
D                 .per_receive_context = ctx
D             };
D             ct_receive_message(connection, &receive_callbacks);
D         }
D         break;
E     default:
E          if (!json_only_mode) {
E             printf("Error: Received message in unknown state\n");
E          }
E          break;
D     }
D }
-
C void on_connection_ready(ct_connection_t* connection) {
C     client_context_t* ctx = (client_context_t*)ct_connection_get_callback_context(connection);
C     ct_message_t* message = NULL;
C     ct_message_context_t* msg_ctx = ct_message_context_new();
E     if (!msg_ctx) {
E         return;
E     }
C     ct_message_context_set_final(msg_ctx, true);
C     switch (ctx->state) {
C     case TRANSFER_NONE_STARTED: {
C         // start large file transfer
C         if (!json_only_mode) {
C             printf("Connection established, starting LARGE file transfer: %s\n",
C                    ct_connection_get_uuid(connection));
C         }
C         timing_end(&ctx->large_stats->handshake_time);
D         timing_start(&ctx->large_stats->transfer_time);
D         message = ct_message_new_with_content("LARGE", 6);
D         ct_send_message_full(connection, message, msg_ctx);
T         ct_message_free(message);
C         ctx->state = STATE_LARGE_STARTED;
D         ct_receive_callbacks_t receive_callbacks = {
D                 .receive_callback = on_msg_received,
D                 .per_receive_context = ctx
D             };
D         ct_receive_message(connection, &receive_callbacks);
C         break;
C     }
C     case STATE_LARGE_DONE: {
C         // start small file transfer
C         if (!json_only_mode) {
C             printf("Connection established, starting SHORT file transfer: %s\n",
C                    ct_connection_get_uuid(connection));
C         }
C         timing_end(&ctx->short_stats->handshake_time);
D         timing_start(&ctx->short_stats->transfer_time);
D         ct_receive_callbacks_t receive_callbacks = {
D                 .receive_callback = on_msg_received,
D                 .per_receive_context = ctx
D             };
D         ct_receive_message(connection, &receive_callbacks);
D         message = ct_message_new_with_content("SHORT", 6);
D         ct_send_message_full(connection, message, msg_ctx);
T         ct_message_free(message);
C         ctx->state = STATE_SHORT_STARTED;
C         break;
C     }
E     default:
E        if (!json_only_mode) {
E            printf("Unexpected state in on_connection_ready\n");
E        }
E        break;
C     }
T     ct_message_context_free(msg_ctx);
C }
-
E void on_establishment_error(ct_connection_t* connection) {
E     if (!json_only_mode)
E         printf("Connection establishment error occurred\n");
T     ct_connection_free(connection);
E }
-
I int main(int argc, char* argv[]) {
I     const char* host = "127.0.0.1";
I     int port = DEFAULT_PORT;
I     int arg_idx = 1;
-
I     if (argc > arg_idx) { host = argv[arg_idx++]; }
I     if (argc > arg_idx) { port = atoi(argv[arg_idx++]); }
-
I     /* Parse --json flag */
I     if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
I         json_only_mode = 1;
I         arg_idx++;
I     }
-
I     if (!json_only_mode) {
I         printf("TAPS Racing Client connecting to %s:%d (prefer QUIC, allow TCP)\n", host, port);
I     }
-
I     client_context_t client_ctx = {0};
I     client_ctx.host = host;
I     client_ctx.port = port;
-
I     transfer_stats_t* large_stats = transfer_stats_new();
I     transfer_stats_t* short_stats = transfer_stats_new();
I     client_ctx.large_stats = large_stats;
I     client_ctx.short_stats = short_stats;
-
I     if (ct_initialize() != 0) {
E         if (json_only_mode) {
E             printf("ERROR\n");
E         } else {
E             fprintf(stderr, "ERROR: Failed to initialize CTaps\n");
E         }
E         return -1;
I     }
-
I     if (!json_only_mode) {
I         printf("\n--- Transferring LARGE file via TAPS (racing) ---\n");
I     }
-
I     ct_set_log_level(CT_LOG_INFO);
-
I     /* Use a pre-parsed IPv4 address rather than a hostname so that no DNS
I      * resolution is needed before candidates can be gathered. */
I     in_addr_t ipv4_addr = inet_addr(host);
E     if (ipv4_addr == INADDR_NONE) {
E         fprintf(stderr, "Invalid IPv4 address: %s\n", host);
E         return 1;
E     }
-
I     ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
E     if (!remote_endpoint) {
E         fprintf(stderr, "Failed to allocate remote endpoint\n");
E         return 1;
E     }
I     ct_remote_endpoint_with_ipv4(remote_endpoint, ipv4_addr);
I     ct_remote_endpoint_with_port(remote_endpoint, port);
-
I     /* Prefer QUIC (multistreaming + msg boundaries), allow TCP as fallback.
I      * REQUIRE reliability rules out raw UDP. */
I     ct_transport_properties_t* transport_properties = ct_transport_properties_new();
E     if (!transport_properties) {
E         fprintf(stderr, "Failed to allocate transport properties\n");
T         ct_remote_endpoint_free(remote_endpoint);
E         return 1;
E     }
-
I     ct_transport_properties_set_reliability(transport_properties, REQUIRE);
I     ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PREFER);
I     ct_transport_properties_set_multistreaming(transport_properties, PREFER);
M     ct_transport_properties_set_multipath(transport_properties, CT_MULTIPATH_ACTIVE);
-
I     /* Security parameters are required for QUIC; ignored if TCP wins the race. */
I     ct_security_parameters_t* security_parameters = ct_security_parameters_new();
E     if (!security_parameters) {
E         fprintf(stderr, "Failed to allocate security parameters\n");
T         ct_transport_properties_free(transport_properties);
T         ct_remote_endpoint_free(remote_endpoint);
E         return 1;
E     }
I     ct_security_parameters_add_alpn(security_parameters, "benchmark");
I     ct_security_parameters_add_client_certificate(security_parameters,
I                                                   RESOURCE_FOLDER "/cert.pem",
I                                                   RESOURCE_FOLDER "/key.pem");
-
I     ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
I     ct_local_endpoint_with_ipv4(local_endpoint, inet_addr("0.0.0.0"));
-
M     ct_local_endpoint_t* local_endpoint2 = ct_local_endpoint_new();
M     ct_local_endpoint_with_ipv4(local_endpoint2, inet_addr("127.0.0.2"));
-
M     ct_local_endpoint_t* endpoints[] = {local_endpoint, local_endpoint2};
-
C     ct_preconnection_t* preconnection = ct_preconnection_new(
C         endpoints, 2, &remote_endpoint, 1, transport_properties, security_parameters);
E     if (!preconnection) {
E         fprintf(stderr, "Failed to allocate preconnection\n");
T         ct_security_parameters_free(security_parameters);
T         ct_transport_properties_free(transport_properties);
T         ct_remote_endpoint_free(remote_endpoint);
E         return 1;
E     }
T     ct_security_parameters_free(security_parameters);
-
C     ct_connection_callbacks_t connection_callbacks = {
C         .ready                  = on_connection_ready,
C         .establishment_error    = on_establishment_error,
C         .per_connection_context = &client_ctx,
C     };
-
C     timing_start(&client_ctx.large_stats->handshake_time);
-
C     int rc = ct_preconnection_initiate(preconnection, &connection_callbacks);
E     if (rc != 0) {
E         fprintf(stderr, "ERROR: Failed to initiate preconnection\n");
T         ct_preconnection_free(preconnection);
T         ct_transport_properties_free(transport_properties);
T         ct_remote_endpoint_free(remote_endpoint);
E         return -1;
E     }
-
C     ct_start_event_loop();
-
T     rc = 0;
T     if (client_ctx.transfer_complete == 1) {
T         char* json = get_json_stats(TRANSFER_MODE_TAPS_RACING,
T                                     client_ctx.large_stats,
T                                     client_ctx.short_stats);
T         if (json) {
T             printf("%s\n", json);
T             free(json);
T         }
E     } else {
E         fprintf(stderr, "ERROR: Transfer failed\n");
E         rc = -1;
E     }
T     ct_close();
T     ct_preconnection_free(preconnection);
T     ct_transport_properties_free(transport_properties);
T     ct_remote_endpoint_free(remote_endpoint);
T     return rc;
T }
