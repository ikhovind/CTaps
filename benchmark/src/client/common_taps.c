#include "../common/timing.h"
#include "../common/file_generator.h"
#include "../common/benchmark_stats.h"
#include "common_taps.h"
#include "ctaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

void initiate_short_transfer(ct_connection_t* large_file_connection) {
    timing_start(&client_ctx.short_stats.handshake_time);
    int clone_res = ct_connection_clone(large_file_connection);
    if (clone_res < 0) {
        printf("Error: Failed to clone connection for SHORT file transfer\n");
        return;
    }
}

int on_msg_received(ct_connection_t *connection, ct_message_t **received_message, ct_message_context_t *ctx) {
    ct_message_t *msg = *received_message;

    switch (client_ctx.state) {
        case TRANSFER_NONE_STARTED:
            printf("Error: Received message in TRANSFER_NONE_STARTED state\n");
            return -1;
            break;
        case STATE_LARGE_STARTED:
            client_ctx.large_stats.bytes_received += msg->length;
            if (client_ctx.large_stats.bytes_received >= LARGE_FILE_SIZE) {
                printf("LARGE file transfers completed.\n");
                timing_end(&client_ctx.large_stats.transfer_time);
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
            client_ctx.short_stats.bytes_received += msg->length;
            if (client_ctx.short_stats.bytes_received >= SHORT_FILE_SIZE) {
                timing_end(&client_ctx.short_stats.transfer_time);
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

int on_connection_ready(ct_connection_t *connection) {
    client_context_t* ctx = (client_context_t*)connection->connection_callbacks.user_connection_context;
    ct_message_t message = {0};
    ct_message_context_t msg_ctx = {0};
    ct_message_properties_build(&msg_ctx.message_properties);
    ct_message_properties_set_final(&msg_ctx.message_properties);
    switch (ctx->state) {
        case TRANSFER_NONE_STARTED:
            // start large file transfer
            printf("Connection established, starting LARGE file transfer: %s\n", connection->uuid);
            timing_end(&ctx->large_stats.handshake_time);
            timing_start(&ctx->large_stats.transfer_time);
            ct_message_build_with_content(&message, "LARGE", 6);
            ct_send_message_full(connection, &message, &msg_ctx);
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
            printf("Connection established, starting SHORT file transfer: %s\n", connection->uuid);
            timing_end(&ctx->short_stats.handshake_time);
            timing_start(&ctx->short_stats.transfer_time);
            ct_message_build_with_content(&message, "SHORT", 6);
            ct_send_message_full(connection, &message, &msg_ctx);
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

int on_establishment_error(ct_connection_t* connection) {
    printf("Connection establishment error occurred\n");
    return -1;
}

