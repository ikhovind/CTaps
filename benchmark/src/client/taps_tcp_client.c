#include "../common/protocol.h"
#include "../common/timing.h"
#include "../common/file_generator.h"
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

    timing_t conn_time_large;
    timing_t transfer_time_large;
    size_t bytes_received_large;

    timing_t conn_time_short;
    timing_t transfer_time_short;
    size_t bytes_received_short;
    int transfer_complete;
} client_context_t;

static client_context_t client_ctx;

static void initiate_short_transfer(ct_connection_t* large_file_connection) {
    int clone_res = ct_connection_clone(large_file_connection);
    if (clone_res < 0) {
        printf("Error: Failed to clone connection for SHORT file transfer\n");
        return;
    }
}

static int on_msg_received(ct_connection_t *connection, ct_message_t **received_message, ct_message_context_t *ctx) {
    ct_message_t *msg = *received_message;

    switch (client_ctx.state) {
        case TRANSFER_NONE_STARTED:
            printf("Error: Received message in TRANSFER_NONE_STARTED state\n");
            return -1;
            break;
        case STATE_LARGE_STARTED:
            client_ctx.bytes_received_large += msg->length;
            if (client_ctx.bytes_received_large >= LARGE_FILE_SIZE) {
                printf("LARGE file transfers completed.\n");
                timing_end(&client_ctx.transfer_time_large);
                client_ctx.state = STATE_LARGE_DONE;
                initiate_short_transfer(connection);
            }
            else {
                ct_receive_message(connection, (ct_receive_callbacks_t){
                    .receive_callback = on_msg_received,
                    .user_receive_context = ctx
                });
            }
            break;
        case STATE_LARGE_DONE:
            printf("Error: Received message in STATE_LARGE_DONE state\n");
            return -1;
            break;
        case STATE_SHORT_STARTED:
            client_ctx.bytes_received_short += msg->length;
            if (client_ctx.bytes_received_short >= SHORT_FILE_SIZE) {
                timing_end(&client_ctx.transfer_time_short);
                client_ctx.state = STATE_BOTH_DONE;
                client_ctx.transfer_complete = 1;
                printf("Both LARGE and SHORT file transfers completed successfully.\n");
                ct_connection_group_close_all(ct_connection_get_connection_group(connection));
            }
            else {
                ct_receive_message(connection, (ct_receive_callbacks_t){
                    .receive_callback = on_msg_received,
                    .user_receive_context = ctx
                });
            }
            break;
        case STATE_BOTH_DONE:
            printf("Error: Received message in STATE_BOTH_DONE state\n");
            return -1;
            break;
    }
    return 0;
}

static int on_connection_ready(ct_connection_t *connection) {
    client_context_t* ctx = (client_context_t*)connection->connection_callbacks.user_connection_context;
    ct_message_t message = {0};
    switch (ctx->state) {
        case TRANSFER_NONE_STARTED:
            // start large file transfer
            printf("Connection established, starting LARGE file transfer\n");
            ct_message_build_with_content(&message, "LARGE", 6);
            ct_send_message(connection, &message);
            ct_message_free_content(&message);
            ctx->state = STATE_LARGE_STARTED;
            ct_receive_message(connection, (ct_receive_callbacks_t){
                .receive_callback = on_msg_received,
                .user_receive_context = ctx
            });
            break;
        case STATE_LARGE_STARTED:
            // error - should not get new connection here
            printf("Unexpected connection established in STATE_LARGE_STARTED\n");
            return -1;
            break;
        case STATE_LARGE_DONE:
            // start small file transfer
            printf("Connection established, starting SHORT file transfer\n");
            ct_message_build_with_content(&message, "SHORT", 6);
            ct_send_message(connection, &message);
            ct_message_free_content(&message);
            ctx->state = STATE_SHORT_STARTED;
            ct_receive_message(connection, (ct_receive_callbacks_t){
                .receive_callback = on_msg_received,
                .user_receive_context = ctx
            });
            break;
        case STATE_SHORT_STARTED:
            // error - should not get new connection here
            printf("Unexpected connection established in STATE_SHORT_STARTED\n");
            return -1;
            break;
        case STATE_BOTH_DONE:
            // error - should not get new connection here
            printf("Unexpected connection established in STATE_BOTH_DONE\n");
            return -1;
            break;
    }
    return 0;
}

static int on_establishment_error(ct_connection_t* connection) {
    printf("Connection establishment error occurred\n");
    return -1;
}

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

    if (ct_initialize(NULL, NULL) != 0) {
        fprintf(stderr, "Failed to initialize CTaps\n");
        return 1;
    }

    printf("\n--- Transferring LARGE file via TAPS ---\n");

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

    ct_connection_t *connection = malloc(sizeof(ct_connection_t));

    ct_connection_callbacks_t connection_callbacks = {
        .ready = on_connection_ready,
        .establishment_error = on_establishment_error,
        .user_connection_context = &client_ctx
    };

    timing_start(&client_ctx.conn_time_large);

    int rc = ct_preconnection_initiate(&preconnection, connection, connection_callbacks);

    ct_start_event_loop();

    if (client_ctx.transfer_complete == 1) {
        ct_close();
        return 0;
    } else {
        fprintf(stderr, "Transfer failed\n");
        ct_close();
        return 1;
    }
}
