//
// Created by ikhovind on 12.08.25.
//
#pragma once

#ifndef TRANSPORT_PROPERTIES_H
#define TRANSPORT_PROPERTIES_H
#include <protocols/protocol_interface.h>

#include "selection_properties/selection_properties.h"
#include "connection_properties/connection_properties.h"

typedef struct {
    SelectionProperties selection_properties;
    ConnectionProperties connection_properties;
} TransportProperties;

void transport_properties_build(TransportProperties *properties);

void selection_properties_set_selection_property(TransportProperties* transport_properties, SelectionProperty selection_property, SelectionPreference preference);

int transport_properties_protocol_stacks_with_selection_properties(TransportProperties* transport_properties, ProtocolImplementation* protocol_stacks, int* num_found);

#endif //TRANSPORT_PROPERTIES_H
