#include <ctaps.h>
#include <stdio.h>
#include <string.h>

void close_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    uint16_t port = ct_local_endpoint_get_resolved_port(
        ct_message_context_get_local_endpoint(message_context)
    );

    printf("Received message: %s on port %d\n", ct_message_get_content(*received_message), port);

    ct_connection_close(connection);
    ct_message_free(*received_message);
}

void on_connection_received_receive_message(ct_listener_t* listener, ct_connection_t* new_connection) {
    printf("Listener received new connection\n");
    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = close_on_message_received,
    };

    ct_receive_message(new_connection, receive_message_request);
    ct_listener_close(listener); // Stop accepting new connections after the first one is received
}

void free_on_listener_closed(ct_listener_t* listener) {
    ct_listener_free(listener);
}

void free_on_connection_closed(ct_connection_t* connection) {
    ct_connection_free(connection);
}

int main() {
    ct_initialize(); // Init logging and event loop
    
    ct_set_log_level(CT_LOG_INFO);

    // Create transport properties
    ct_transport_properties_t* listener_props = ct_transport_properties_new();
    ct_transport_properties_set_preserve_msg_boundaries(listener_props, PROHIBIT); // force TCP

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ct_local_endpoint_with_port(local_endpoint, 1234);

    // Create preconnection
    ct_preconnection_t* preconnection = ct_preconnection_new(local_endpoint,
                                                             1,
                                                             NULL,
                                                             0,
                                                             listener_props,
                                                             NULL
                                                             );

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = on_connection_received_receive_message,
        .listener_closed = free_on_listener_closed
    };

    ct_connection_callbacks_t connection_callbacks = {
        .closed = free_on_connection_closed 
    };

    int rc = ct_preconnection_listen(preconnection, listener_callbacks, &connection_callbacks);

    if (rc < 0) {
        perror("Sync error in establishing listener\n");
        return -1;
    }

    ct_start_event_loop();

    // Cleanup
    ct_preconnection_free(preconnection);
    ct_transport_properties_free(listener_props);
    ct_local_endpoint_free(local_endpoint);
    ct_close();

    return 0;
}
