#include <ctaps.h>
#include <stdio.h>
#include <string.h>



int close_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    printf("Message received!!!...\n");
    const char* msg_cont = ct_message_get_content(*received_message);
    for (size_t i = 0; i < ct_message_get_length(*received_message); i++) {
        printf("0x%02x \n", (unsigned char)msg_cont[i]);
    }
    printf("Message content (hex): \n");
    // printf("Message length: %zu\n", ct_message_get_length(*received_message));
    //const char* msg_cont = ct_message_get_content(*received_message);
    ct_connection_close(connection);
    return 0;
}

int establishment_error(ct_connection_t* connection) {
    printf("Connection establishment failed!!!...\n");
    return 0;
}

int send_message_and_receive(struct ct_connection_s* connection) {
    printf("Connection is ready!!!...\n");

    ct_receive_callbacks_t receive_message_request = {
       .receive_callback = close_on_message_received,
    };
    
    ct_receive_message(connection, receive_message_request);
    return 0;
}

int main() {
    ct_initialize(); // Init (currently) global state
    //
    ct_set_log_level(CT_LOG_DEBUG);


    // Create remote endpoint (where we will try to connect to)
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_port(remote_endpoint, 443);
    ct_remote_endpoint_with_hostname(remote_endpoint, "browserleaks.com");

   // Create transport properties
   ct_transport_properties_t* transport_properties = ct_transport_properties_new();

   // selection properties decide which protocol(s) will be used, if multiple are compatible with
   // our requirements, then we will race the protocols
   // TCP is the only protocol compatible with this requirement
   ct_transport_properties_set_multistreaming(transport_properties, REQUIRE); // force QUIC
    //
   ct_security_parameters_t* security_parameters = ct_security_parameters_new();
   const char* alpn_strings = "h3";
   ct_security_parameters_add_alpn(security_parameters, alpn_strings);


   ct_security_parameters_add_client_certificate(security_parameters, "/home/ikhovind/Documents/Skole/taps/test/quic/cert.pem", "/home/ikhovind/Documents/Skole/taps/test/quic/key.pem");

   // Create preconnection
   ct_preconnection_t* preconnection = ct_preconnection_new(NULL, 0, remote_endpoint, 1, transport_properties, security_parameters);

   ct_connection_callbacks_t connection_callbacks = {
       .ready = send_message_and_receive,
       .establishment_error = establishment_error,
   };

   int rc = ct_preconnection_initiate(preconnection, connection_callbacks); // Gather potential endpoints and start racing, when event loop starts

   if (rc < 0) {
      perror("Error in initiating connection\n");
      return rc;
   }

   ct_start_event_loop(); // Start the libuv event loop, block until all connections close (or no connection can be established)

   // Cleanup
   ct_preconnection_free(preconnection);
   ct_transport_properties_free(transport_properties);
   ct_remote_endpoint_free(remote_endpoint);
   ct_security_parameters_free(security_parameters);
   ct_close();

   return 0;
}
