//
// Created by ikhovind on 12.08.25.
//

#include "preconnection.h"


void preconnection_build(Preconnection *preconnection, const TransportProperties transport_properties) {
    printf("initializing preconnection\n");
    preconnection->transport_properties = transport_properties;
}

void preconnection_initiate(Preconnection *preconnection, Connection *connection) {
    int num_found_protocols = 0;
    transport_properties_protocol_stacks_with_selection_properties(&preconnection->transport_properties, connection->protocol, &num_found_protocols);
    if (num_found_protocols > 0) {
        printf("Initializing protocol");
        connection->protocol->init();
    }
    else {
        printf("No protocol found");
    }
}


