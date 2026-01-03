#include "common_taps.h"

client_context_t client_ctx;
int json_only_mode = 0;

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
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

    if (!json_only_mode) printf("TAPS TCP Client connecting to %s:%d\n", host, port);

    memset(&client_ctx, 0, sizeof(client_ctx));
    client_ctx.host = host;
    client_ctx.port = port;

    if (ct_initialize(RESOURCE_FOLDER "/cert.pem", RESOURCE_FOLDER "/key.pem") != 0) {
        fprintf(stderr, "ERROR: Failed to initialize CTaps\n");
        return -1;
    }

    if (!json_only_mode) printf("\n--- Transferring LARGE file via TAPS ---\n");

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, client_ctx.host);
    ct_remote_endpoint_with_port(&remote_endpoint, client_ctx.port);

    ct_transport_properties_t* transport_properties = ct_transport_properties_new();
    if (!transport_properties) {
        fprintf(stderr, "Failed to allocate transport properties\n");
        return 1;
    }

    ct_tp_set_sel_prop_preference(transport_properties, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(transport_properties, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

    ct_security_parameters_t security_parameters;
    ct_security_parameters_build(&security_parameters);
    char* alpn_strings = "benchmark";
    ct_sec_param_set_property_string_array(&security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t* preconnection = ct_preconnection_new(&remote_endpoint, 1, transport_properties, &security_parameters);
    if (!preconnection) {
        fprintf(stderr, "Failed to allocate preconnection\n");
        ct_transport_properties_free(transport_properties);
        return 1;
    }

    ct_connection_callbacks_t connection_callbacks = {
        .ready = on_connection_ready,
        .establishment_error = on_establishment_error,
        .user_connection_context = &client_ctx
    };

    timing_start(&client_ctx.large_stats.handshake_time);

    int rc = ct_preconnection_initiate(preconnection, connection_callbacks);

    ct_start_event_loop();

    if (client_ctx.transfer_complete == 1) {
        char *json = get_json_stats(TRANSFER_MODE_TAPS, &client_ctx.large_stats, &client_ctx.short_stats, 1);
        if (json) {
            printf("%s\n", json);
            free(json);
        }
        ct_close();
        ct_preconnection_free(preconnection);
        ct_transport_properties_free(transport_properties);
        return 0;
    } else {
        fprintf(stderr, "ERROR: Transfer failed\n");
        ct_close();
        ct_preconnection_free(preconnection);
        ct_transport_properties_free(transport_properties);
        return -1;
    }
}
