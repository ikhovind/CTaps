//
// Created by ikhovind on 12.08.25.
//

#include "transport_properties.h"

#include <protocols/registry/protocol_registry.h>

#include "selection_properties/selection_properties.h"

void transport_properties_build(TransportProperties* properties) {
  return;
}

void selection_properties_set_selection_property(
    TransportProperties* transport_properties,
    SelectionProperty selection_property, SelectionPreference preference) {
  selection_properties_set(&transport_properties->selection_properties,
                           selection_property, preference);
}

int transport_properties_protocol_stacks_with_selection_properties(
    TransportProperties* transport_properties,
    ProtocolImplementation* protocol_stacks, int* num_found) {
  ProtocolImplementation** supported_protocols = get_supported_protocols();
  printf("supported protocols: %s\n", supported_protocols[0]->name);
  *protocol_stacks = *supported_protocols[0];
  *num_found = 1;
  return 0;
}
