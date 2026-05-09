#include "ctaps.h"
#include "../common/protocol.h"
#include "../common/timing.h"
#include "../common/file_generator.h"
#include "../common/benchmark_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef enum {
    TRANSFER_NONE_STARTED,
    STATE_LARGE_STARTED,
    STATE_LARGE_DONE,
    STATE_SHORT_STARTED,
    STATE_BOTH_DONE,
} transfer_progress_t;

typedef struct {
    const char* host;
    int port;

    transfer_progress_t state;

    transfer_stats_t* large_stats;
    transfer_stats_t* short_stats;
    int transfer_complete;
} client_context_t;


int json_only_mode = 0;

void initiate_short_transfer(ct_connection_t* large_file_connection) {
    client_context_t* client_ctx = ct_connection_get_callback_context(large_file_connection);
    timing_start(&client_ctx->short_stats->handshake_time);
    int clone_res = ct_connection_clone(large_file_connection);
    if (clone_res < 0) {
        if (!json_only_mode) {
            printf("Error: Failed to clone connection for SHORT file transfer\n");
        }
        return;
    }
}

void on_msg_received(ct_connection_t* connection, ct_message_t* msg,
                    ct_message_context_t* ctx) {
    unsigned int msg_length = ct_message_get_length(msg);
    client_context_t* client_ctx = ct_connection_get_callback_context(connection);

    switch (client_ctx->state) {
    case STATE_LARGE_STARTED:

        client_ctx->large_stats->bytes_received += msg_length;
        time_received_chunk(client_ctx->large_stats, msg_length);
        if (client_ctx->large_stats->bytes_received >= LARGE_FILE_SIZE) {
            if (!json_only_mode) {
                printf("LARGE file transfers of size %d completed.\n", LARGE_FILE_SIZE);
            }
            timing_end(&client_ctx->large_stats->transfer_time);
            client_ctx->state = STATE_LARGE_DONE;
            initiate_short_transfer(connection);
        } else {
                ct_receive_callbacks_t receive_callbacks = {
                    .receive_callback = on_msg_received,
                    .per_receive_context = ctx
                };
            ct_receive_message(connection, &receive_callbacks);
        }
        break;
    case STATE_SHORT_STARTED:
        client_ctx->short_stats->bytes_received += msg_length;
        if (client_ctx->short_stats->bytes_received >= SHORT_FILE_SIZE) {
            timing_end(&client_ctx->short_stats->transfer_time);
            client_ctx->state = STATE_BOTH_DONE;
            client_ctx->transfer_complete = 1;
            if (!json_only_mode) {
                printf("Both LARGE and SHORT file transfers completed successfully.\n");
         }
            ct_connection_close_group(connection);
        } else {
            ct_receive_callbacks_t receive_callbacks = {
                .receive_callback = on_msg_received,
                .per_receive_context = ctx
            };
            ct_receive_message(connection, &receive_callbacks);
        }
        break;
    default:
         if (!json_only_mode) {
            printf("Error: Received message in unknown state\n");
         }
         break;
    }
}

void on_connection_ready(ct_connection_t* connection) {
    client_context_t* ctx = (client_context_t*)ct_connection_get_callback_context(connection);
    ct_message_t* message = NULL;
    ct_message_context_t* msg_ctx = ct_message_context_new();
    if (!msg_ctx) {
        return;
    }
    ct_message_context_set_final(msg_ctx, true);
    switch (ctx->state) {
    case TRANSFER_NONE_STARTED: {
        // start large file transfer
        if (!json_only_mode) {
            printf("Connection established, starting LARGE file transfer: %s\n",
                   ct_connection_get_uuid(connection));
        }
        timing_end(&ctx->large_stats->handshake_time);
        timing_start(&ctx->large_stats->transfer_time);
        message = ct_message_new_with_content("LARGE", 6);
        ct_send_message_full(connection, message, msg_ctx);
        ct_message_free(message);
        ctx->state = STATE_LARGE_STARTED;
        ct_receive_callbacks_t receive_callbacks = {
                .receive_callback = on_msg_received,
                .per_receive_context = ctx
            };
        ct_receive_message(connection, &receive_callbacks);
        break;
    }
    case STATE_LARGE_DONE: {
        // start small file transfer
        if (!json_only_mode) {
            printf("Connection established, starting SHORT file transfer: %s\n",
                   ct_connection_get_uuid(connection));
        }
        timing_end(&ctx->short_stats->handshake_time);
        timing_start(&ctx->short_stats->transfer_time);
        ct_receive_callbacks_t receive_callbacks = {
                .receive_callback = on_msg_received,
                .per_receive_context = ctx
            };
        ct_receive_message(connection, &receive_callbacks);
        message = ct_message_new_with_content("SHORT", 6);
        ct_send_message_full(connection, message, msg_ctx);
        ct_message_free(message);
        ctx->state = STATE_SHORT_STARTED;
        break;
    }
    default:
       if (!json_only_mode) {
           printf("Unexpected state in on_connection_ready\n");
       }
       break;
    }
    ct_message_context_free(msg_ctx);
}

void on_establishment_error(ct_connection_t* connection) {
    if (!json_only_mode)
        printf("Connection establishment error occurred\n");
    ct_connection_free(connection);
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    int port = DEFAULT_PORT;
    int arg_idx = 1;

    if (argc > arg_idx) { host = argv[arg_idx++]; }
    if (argc > arg_idx) { port = atoi(argv[arg_idx++]); }

    /* Parse --json flag */
    if (argc > arg_idx && strcmp(argv[arg_idx], "--json") == 0) {
        json_only_mode = 1;
        arg_idx++;
    }

    if (!json_only_mode) {
        printf("TAPS Racing Client connecting to %s:%d (prefer QUIC, allow TCP)\n", host, port);
    }

    client_context_t client_ctx = {0};
    client_ctx.host = host;
    client_ctx.port = port;

    transfer_stats_t* large_stats = transfer_stats_new();
    transfer_stats_t* short_stats = transfer_stats_new();
    client_ctx.large_stats = large_stats;
    client_ctx.short_stats = short_stats;

    if (ct_initialize() != 0) {
        if (json_only_mode) {
            printf("ERROR\n");
        } else {
            fprintf(stderr, "ERROR: Failed to initialize CTaps\n");
        }
        return -1;
    }

    if (!json_only_mode) {
        printf("\n--- Transferring LARGE file via TAPS (racing) ---\n");
    }

    ct_set_log_level(CT_LOG_INFO);

    /* Use a pre-parsed IPv4 address rather than a hostname so that no DNS
     * resolution is needed before candidates can be gathered. */
    in_addr_t ipv4_addr = inet_addr(host);
    if (ipv4_addr == INADDR_NONE) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", host);
        return 1;
    }

    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    if (!remote_endpoint) {
        fprintf(stderr, "Failed to allocate remote endpoint\n");
        return 1;
    }
    ct_remote_endpoint_with_ipv4(remote_endpoint, ipv4_addr);
    ct_remote_endpoint_with_port(remote_endpoint, port);
    const ct_remote_endpoint_t* remotes[] = {remote_endpoint};

    /* Prefer QUIC (multistreaming + msg boundaries), allow TCP as fallback.
     * REQUIRE reliability rules out raw UDP. */
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    if (!transport_properties) {
        fprintf(stderr, "Failed to allocate transport properties\n");
        ct_remote_endpoint_free(remote_endpoint);
        return 1;
    }

    ct_transport_properties_set_reliability(transport_properties, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PREFER);
    ct_transport_properties_set_multistreaming(transport_properties, PREFER);
    ct_transport_properties_set_multipath(transport_properties, CT_MULTIPATH_ACTIVE);

    /* Security parameters are required for QUIC; ignored if TCP wins the race. */
    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    if (!security_parameters) {
        fprintf(stderr, "Failed to allocate security parameters\n");
        ct_transport_properties_free(transport_properties);
        ct_remote_endpoint_free(remote_endpoint);
        return 1;
    }
    ct_security_parameters_add_alpn(security_parameters, "benchmark");
    ct_security_parameters_add_client_certificate(security_parameters,
                                                  RESOURCE_FOLDER "/cert.pem",
                                                  RESOURCE_FOLDER "/key.pem");

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ct_local_endpoint_with_ipv4(local_endpoint, inet_addr("0.0.0.0"));

    ct_local_endpoint_t* local_endpoint2 = ct_local_endpoint_new();
    ct_local_endpoint_with_ipv4(local_endpoint2, inet_addr("127.0.0.2"));

    const ct_local_endpoint_t* endpoints[] = {local_endpoint, local_endpoint2};

    ct_preconnection_t* preconnection = ct_preconnection_new(
        endpoints, 2, remotes, 1, transport_properties, security_parameters);
    if (!preconnection) {
        fprintf(stderr, "Failed to allocate preconnection\n");
        ct_security_parameters_free(security_parameters);
        ct_transport_properties_free(transport_properties);
        ct_remote_endpoint_free(remote_endpoint);
        return 1;
    }
    ct_security_parameters_free(security_parameters);

    ct_connection_callbacks_t connection_callbacks = {
        .ready                  = on_connection_ready,
        .establishment_error    = on_establishment_error,
        .per_connection_context = &client_ctx,
    };

    timing_start(&client_ctx.large_stats->handshake_time);

    int rc = ct_preconnection_initiate(preconnection, &connection_callbacks);
    if (rc != 0) {
        fprintf(stderr, "ERROR: Failed to initiate preconnection\n");
        ct_preconnection_free(preconnection);
        ct_transport_properties_free(transport_properties);
        ct_remote_endpoint_free(remote_endpoint);
        return -1;
    }

    ct_start_event_loop();

    rc = 0;
    if (client_ctx.transfer_complete == 1) {
        char* json = get_json_stats(TRANSFER_MODE_TAPS_RACING,
                                    client_ctx.large_stats,
                                    client_ctx.short_stats);
        if (json) {
            printf("%s\n", json);
            free(json);
        }
    } else {
        fprintf(stderr, "ERROR: Transfer failed\n");
        rc = -1;
    }
    ct_close();
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_remote_endpoint_free(remote_endpoint);
    return rc;
}
