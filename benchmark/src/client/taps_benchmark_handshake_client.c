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

typedef struct {
    const char* host;
    int port;

    transfer_stats_t* large_stats;
    transfer_stats_t* short_stats;
    int transfer_complete;
} client_context_t;


int json_only_mode = 0;

void on_connection_ready(ct_connection_t* connection) {
    client_context_t* ctx = (client_context_t*)ct_connection_get_callback_context(connection);
    if (!json_only_mode) {
        printf("Connection established, starting LARGE file transfer: %s\n",
               ct_connection_get_uuid(connection));
    }
    timing_end(&ctx->large_stats->handshake_time);
    ct_connection_close_group(connection);
}

void on_establishment_error(ct_connection_t* connection) {
    if (!json_only_mode)
        printf("Connection establishment error occurred\n");
    ct_connection_free(connection);
}

void free_on_close(ct_connection_t* connection) {
    ct_connection_free(connection);
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    const char* h2 = "127.0.0.2";
    int port = DEFAULT_PORT;
    int arg_idx = 1;

    if (argc > arg_idx) {
        host = argv[arg_idx];
        arg_idx++;
    }
    if (argc > arg_idx) {
        port = atoi(argv[arg_idx]);
        arg_idx++;
    }

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

    ct_set_log_level(CT_LOG_DEBUG);

    /* Use a pre-parsed IPv4 address rather than a hostname so that no DNS
     * resolution is needed before candidates can be gathered. */
    in_addr_t ip1 = inet_addr(host);
    in_addr_t ip2 = inet_addr(h2);

    /* Prefer QUIC (multistreaming + msg boundaries), allow TCP as fallback.
     * REQUIRE reliability rules out raw UDP. */
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    if (!transport_properties) {
        fprintf(stderr, "Failed to allocate transport properties\n");
        return 1;
    }

    ct_transport_properties_set_reliability(transport_properties, REQUIRE);
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PREFER);
    ct_transport_properties_set_multistreaming(transport_properties, PREFER);

    /* Security parameters are required for QUIC; ignored if TCP wins the race. */
    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    if (!security_parameters) {
        fprintf(stderr, "Failed to allocate security parameters\n");
        ct_transport_properties_free(transport_properties);
        return 1;
    }
    ct_security_parameters_add_alpn(security_parameters, "benchmark");
    ct_security_parameters_add_client_certificate(security_parameters,
                                                  RESOURCE_FOLDER "/cert.pem",
                                                  RESOURCE_FOLDER "/key.pem");


    ct_remote_endpoint_t* r1 = ct_remote_endpoint_new();
    if (!r1) {
        fprintf(stderr, "Failed to allocate remote endpoint\n");
        return 1;
    }
    ct_remote_endpoint_with_ipv4(r1, ip1);
    ct_remote_endpoint_with_port(r1, port);

    ct_remote_endpoint_t* r2 = ct_remote_endpoint_new();
    if (!r2) {
        fprintf(stderr, "Failed to allocate second remote endpoint\n");
        ct_remote_endpoint_free(r1);
        return 1;
    }

    ct_remote_endpoint_with_ipv4(r2, ip2);
    ct_remote_endpoint_with_port(r2, port);

    ct_remote_endpoint_t* endpoints[] = {r1, r2};

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    if (!local_endpoint) {
        fprintf(stderr, "Failed to allocate local endpoint\n");
        ct_security_parameters_free(security_parameters);
        ct_transport_properties_free(transport_properties);
        ct_remote_endpoint_free(r1);
        ct_remote_endpoint_free(r2);
        return 1;
    }
    ct_local_endpoint_with_ipv4(local_endpoint, inet_addr("127.0.0.1"));

    ct_preconnection_t* preconnection = ct_preconnection_new(
        &local_endpoint, 1, endpoints, 2, transport_properties, security_parameters);
    if (!preconnection) {
        fprintf(stderr, "Failed to allocate preconnection\n");
        ct_security_parameters_free(security_parameters);
        ct_transport_properties_free(transport_properties);
        ct_remote_endpoint_free(r1);
        ct_remote_endpoint_free(r2);
        return 1;
    }
    ct_security_parameters_free(security_parameters);

    ct_connection_callbacks_t connection_callbacks = {
        .ready                  = on_connection_ready,
        .establishment_error    = on_establishment_error,
        .closed                 = free_on_close,
        .per_connection_context = &client_ctx,
    };

    timing_start(&client_ctx.large_stats->handshake_time);

    int rc = ct_preconnection_initiate(preconnection, &connection_callbacks);
    if (rc != 0) {
        fprintf(stderr, "ERROR: Failed to initiate preconnection\n");
        ct_preconnection_free(preconnection);
        ct_transport_properties_free(transport_properties);
        ct_remote_endpoint_free(r1);
        ct_remote_endpoint_free(r2);
        return -1;
    }

    ct_start_event_loop();

    char* json = get_json_stats(TRANSFER_MODE_TAPS_RACING,
                                client_ctx.large_stats,
                                client_ctx.short_stats);
    if (json) {
        printf("%s\n", json);
        free(json);
    }
    ct_close();
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_remote_endpoint_free(r1);
    ct_remote_endpoint_free(r2);
    return rc;
}
