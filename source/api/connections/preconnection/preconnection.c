//
// Created by ikhovind on 12.08.25.
//

#include "preconnection.h"


void preconnection_init(Preconnection *preconnection, TransportProperties transport_properties) {
    printf("initializing preconnection\n");
    preconnection->transport_properties = transport_properties;
}

