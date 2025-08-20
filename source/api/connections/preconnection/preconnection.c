//
// Created by ikhovind on 12.08.25.
//

#include "preconnection.h"


void preconnection_build(Preconnection *preconnection, const TransportProperties transport_properties, RemoteEndpoint remote_endpoint) {
    printf("initializing preconnection\n");
    preconnection->transport_properties = transport_properties;
    preconnection->remote = remote_endpoint;
}

void preconnection_initiate(Preconnection *preconnection, Connection *connection) {
    printf("Initiating preconnection\n");
    int num_found_protocols = 0;
    transport_properties_protocol_stacks_with_selection_properties(&preconnection->transport_properties, &connection->protocol, &num_found_protocols);
    if (num_found_protocols > -1) {
        printf("Initializing protocol\n");
        connection->protocol.init();
        connection->remote_endpoint = preconnection->remote;
    }
    else {
        printf("No protocol found\n");
    }
}


