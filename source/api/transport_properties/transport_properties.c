//
// Created by ikhovind on 12.08.25.
//

#include "transport_properties.h"

#include <protocols/registry/protocol_registry.h>

#include "selection_properties/selection_properties.h"

void transport_properties_build(TransportProperties* properties) {
  selection_properties_init(&properties->selection_properties);
}


int transport_properties_protocol_stacks_with_selection_properties(
    TransportProperties* transport_properties,
    ProtocolImplementation* protocol_stacks, int* num_found) {
  ProtocolImplementation** supported_protocols = get_supported_protocols();
  printf("supported protocols: %s\n", supported_protocols[0]->name);
  *protocol_stacks = *supported_protocols[0];
  printf("Set protocol stacks\n");
  *num_found = 1;
  return 0;
}


void tp_set_sel_prop_preference(TransportProperties* props, SelectionPropertyEnum prop_enum, SelectionPreference val) {
  set_sel_prop_preference(&props->selection_properties, prop_enum, val);  // Call the actual function to
}

void tp_set_sel_prop_multipath(TransportProperties* props, SelectionPropertyEnum prop_enum, MultipathEnum val) {
  set_sel_prop_multipath(&props->selection_properties, prop_enum, val);
}

void tp_set_sel_prop_direction(TransportProperties* props, SelectionPropertyEnum prop_enum, DirectionOfCommunicationEnum val) {
  set_sel_prop_direction(&props->selection_properties, prop_enum, val);
}

void tp_set_sel_prop_bool(TransportProperties* props, SelectionPropertyEnum prop_enum, bool val) {
  set_sel_prop_bool(&props->selection_properties, prop_enum, val);
}
void tp_set_sel_prop(TransportProperties* props, SelectionPropertyEnum prop_enum, SelectionPropertyValue val) {
  set_sel_prop(&props->selection_properties, prop_enum, val);
}
