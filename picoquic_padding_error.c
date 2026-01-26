#include <picoquic.h>
#include <picoquic_packet_loop.h>
#include <picoquic_utils.h>

// Forward declare logging function
int picoquic_set_textlog(picoquic_quic_t* quic, char const* textlog_file);
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALPN "complicated-ping"
#define SNI "localhost"
#define SERVER_PORT 4433
#define TIMEOUT_US 5000000  // 5 second timeout

static bool connection_ready = false;
static bool connection_closed = false;
static picoquic_cnx_t* client_cnx = NULL;
static uint64_t start_time = 0;

int client_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    (void)cnx;
    (void)stream_id;
    (void)bytes;
    (void)length;
    (void)callback_ctx;
    (void)v_stream_ctx;

    printf("Callback event: %d\n", fin_or_event);

    switch (fin_or_event) {
        case picoquic_callback_ready:
            printf("Connection ACCEPTED!\n");
            connection_ready = true;
            break;
        case picoquic_callback_close:
            printf("Connection REJECTED: server closed connection\n");
            connection_closed = true;
            break;
        case picoquic_callback_application_close:
            printf("Connection REJECTED: application close\n");
            connection_closed = true;
            break;
        case picoquic_callback_stateless_reset:
            printf("Connection REJECTED: stateless reset\n");
            connection_closed = true;
            break;
        case picoquic_callback_almost_ready:
            printf("Connection almost ready...\n");
            break;
        default:
            break;
    }
    return 0;
}

int loop_callback(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
    void* callback_ctx, void* callback_arg)
{
    (void)quic;
    (void)callback_ctx;
    (void)callback_arg;

    // Print state on each callback for debugging
    if (client_cnx != NULL && cb_mode == picoquic_packet_loop_after_receive) {
        picoquic_state_enum state = picoquic_get_cnx_state(client_cnx);
        uint64_t local_err = picoquic_get_local_error(client_cnx);
        uint64_t remote_err = picoquic_get_remote_error(client_cnx);
        printf("After receive - state: %d, local_err: %lu, remote_err: %lu\n",
               state, (unsigned long)local_err, (unsigned long)remote_err);
    }

    // Check if callback already set our flags
    if (connection_ready || connection_closed) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }

    // Check connection state and errors
    if (client_cnx != NULL) {
        picoquic_state_enum state = picoquic_get_cnx_state(client_cnx);
        uint64_t remote_err = picoquic_get_remote_error(client_cnx);

        if (state == picoquic_state_disconnected) {
            printf("Connection REJECTED: disconnected\n");
            connection_closed = true;
            return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        }

        if (remote_err != 0) {
            printf("Connection REJECTED: remote error %lu (0x%lx)\n",
                   (unsigned long)remote_err, (unsigned long)remote_err);
            connection_closed = true;
            return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        }
    }

    // Check for timeout (only as fallback)
    uint64_t now = picoquic_current_time();
    if (now - start_time > TIMEOUT_US) {
        printf("Connection REJECTED: timeout\n");
        connection_closed = true;
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }

    return 0;
}

int main(void)
{
    int ret = 0;
    picoquic_quic_t* quic = NULL;
    struct sockaddr_in server_addr;

    printf("Attempting QUIC connection to %s:%d with ALPN '%s'\n", SNI, SERVER_PORT, ALPN);

    start_time = picoquic_current_time();

    // Create picoquic context (client mode - no cert/key needed for client)
    quic = picoquic_create(
        1,          // max connections
        NULL,       // cert file (not needed for client)
        NULL,       // key file (not needed for client)
        NULL,       // cert_root_file_name
        ALPN,       // default alpn
        client_callback,
        NULL,       // callback context
        NULL,       // cnx_id_callback
        NULL,       // cnx_id_callback_ctx
        NULL,       // reset_seed
        picoquic_current_time(),
        NULL,       // simulated_time
        NULL,       // ticket_file_name
        NULL,       // ticket_encryption_key
        0           // ticket_encryption_key_length
    );

    if (quic == NULL) {
        fprintf(stderr, "Failed to create picoquic context\n");
        ret = 1;
        goto cleanup;
    }

    // Enable logging to see what's happening
    picoquic_set_textlog(quic, "picoquic.log");
    picoquic_set_log_level(quic, 3);  // Maximum verbosity

    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Create QUIC connection
    client_cnx = picoquic_create_cnx(
        quic,
        picoquic_null_connection_id,
        picoquic_null_connection_id,
        (struct sockaddr*)&server_addr,
        picoquic_current_time(),
        0,          // proposed version (0 = default)
        SNI,        // sni
        ALPN,       // alpn
        1           // client mode
    );

    if (client_cnx == NULL) {
        fprintf(stderr, "Failed to create QUIC connection\n");
        ret = 1;
        goto cleanup;
    }

    // Start the client connection
    ret = picoquic_start_client_cnx(client_cnx);
    if (ret != 0) {
        fprintf(stderr, "Failed to start client connection: %d\n", ret);
        goto cleanup;
    }

    printf("Connection initiated, waiting for response...\n");

    // Run the packet loop - this handles all socket I/O
    ret = picoquic_packet_loop(quic, 0, server_addr.sin_family, 0, 0, 0, loop_callback, NULL);

    if (ret != 0 && ret != PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP) {
        fprintf(stderr, "Packet loop error: %d\n", ret);
        ret = 1;
    } else if (connection_ready) {
        printf("Successfully established connection!\n");
        ret = 0;
    } else {
        ret = 1;
    }

cleanup:
    if (quic != NULL) {
        picoquic_free(quic);
    }

    return ret;
}
