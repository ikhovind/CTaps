#include <arpa/inet.h>
#include <ctaps.h>
#include <stdio.h>
#include <string.h>

int close_on_message_received(ct_connection_t* connection, 
                              ct_message_t** received_message,
                              ct_message_context_t* message_context) {

    uint16_t port = ct_local_endpoint_get_resolved_port(
        ct_message_context_get_local_endpoint(message_context)
    );

    printf("Received message: %s on port %d\n", ct_message_get_content(*received_message), port);

    ct_connection_close(connection);
    return 0;
}

int send_message_and_receive(struct ct_connection_s* connection) {
    ct_message_t* message = ct_message_new_with_content("ping", strlen("ping") + 1);
    // CTaps takes a deep copy of the passed content, so the message can be freed after this returns
    ct_send_message(connection, message);
    ct_message_free(message);

    ct_receive_callbacks_t receive_message_request = {
        .receive_callback = close_on_message_received,
    };

    ct_receive_message(connection, receive_message_request);
    return 0;
}

int main() {
    ct_initialize(); // Init (currently) global state

    // Create remote endpoint (where we will try to connect to)
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 1234); // example port

    // Create transport properties
    ct_transport_properties_t* transport_properties = ct_transport_properties_new();

    // selection properties decide which protocol(s) will be used, if multiple are compatible with
    // our requirements, then we will race the protocols
    // TCP is the only protocol compatible with this requirement
    ct_transport_properties_set_preserve_msg_boundaries(transport_properties, PROHIBIT);

    // Create preconection
    ct_preconnection_t* preconnection = ct_preconnection_new(NULL, // No local endpoint to bind to ephemeral port
                                                             0,
                                                             remote_endpoint,
                                                             1,
                                                             transport_properties,
                                                             NULL
                                                             );

    ct_connection_callbacks_t connection_callbacks = {
        .ready = send_message_and_receive,
    };

    int rc = ct_preconnection_initiate(
        preconnection,
        connection_callbacks); // Gather potential endpoints and start racing, when event loop starts

    if (rc < 0) {
        perror("Error in initiating connection\n");
        return rc;
    }

    ct_start_event_loop(); // Start the libuv event loop, block until all connections close (or no connection can be established)

    // Cleanup
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(transport_properties);
    ct_remote_endpoint_free(remote_endpoint);
    ct_close();

    return 0;
}
