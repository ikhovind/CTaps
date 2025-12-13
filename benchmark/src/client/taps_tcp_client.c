#include "common_taps.h"

client_context_t client_ctx;

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = atoi(argv[2]);
    }

    printf("TAPS TCP Client connecting to %s:%d\n", host, port);

    memset(&client_ctx, 0, sizeof(client_ctx));
    client_ctx.host = host;
    client_ctx.port = port;
    client_ctx.state = TRANSFER_NONE_STARTED;
    client_ctx.transfer_complete = 0;
    memset(&client_ctx.large_stats, 0, sizeof(transfer_stats_t));
    memset(&client_ctx.short_stats, 0, sizeof(transfer_stats_t));

    if (ct_initialize(NULL, NULL) != 0) {
        fprintf(stderr, "Failed to initialize CTaps\n");
        return 1;
    }

    printf("\n--- Transferring LARGE file via TAPS ---\n");

    ct_set_log_level(CT_LOG_WARN);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, client_ctx.host);
    ct_remote_endpoint_with_port(&remote_endpoint, client_ctx.port);

    ct_transport_properties_t transport_properties;
    ct_transport_properties_build(&transport_properties);

    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, PROHIBIT);

    ct_preconnection_t preconnection;
    ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

    ct_connection_callbacks_t connection_callbacks = {
        .ready = on_connection_ready,
        .establishment_error = on_establishment_error,
        .user_connection_context = &client_ctx
    };

    timing_start(&client_ctx.large_stats.handshake_time);

    // Use new v2 API - connection provided in ready() callback, no pre-allocation needed
    int rc = ct_preconnection_initiate_v2(&preconnection, connection_callbacks);

    ct_start_event_loop();

    if (client_ctx.transfer_complete == 1) {
        char *json = get_json_stats(TRANSFER_MODE_TAPS, &client_ctx.large_stats, &client_ctx.short_stats, 0);
        if (json) {
            printf("\n%s\n", json);
            free(json);
        }
        ct_close();
        return 0;
    } else {
        fprintf(stderr, "Transfer failed\n");
        ct_close();
        return 1;
    }
}
