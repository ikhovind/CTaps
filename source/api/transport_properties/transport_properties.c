#include <stdbool.h>

#include "transport_properties.h"

#include "connection_properties/connection_properties.h"
#include "protocols/protocol_interface.h"
#include "selection_properties/selection_properties.h"

void transport_properties_build(TransportProperties* properties) {
  selection_properties_build(&properties->selection_properties);
  connection_properties_build(&properties->connection_properties);
}

// This is supposed to be invoked when initiating a preconnection,
// so it assumes that the selection properties have been set according to
// what kind of connection is desired.
bool protocol_implementation_supports_selection_properties(
  const ProtocolImplementation* protocol,
  const SelectionProperties* selection_properties) {
  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    SelectionProperty desired_value = selection_properties->selection_property[i];
    SelectionProperty protocol_value = protocol->selection_properties.selection_property[i];

    if (desired_value.type == TYPE_PREFERENCE) {
      if (desired_value.value.simple_preference == REQUIRE && protocol_value.value.simple_preference == PROHIBIT) {
        return false;
      }
      if (desired_value.value.simple_preference == PROHIBIT && protocol_value.value.simple_preference == REQUIRE) {
        return false;
      }
    }
  }
  return true;
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

void tp_set_sel_prop_interface(TransportProperties* props, char* interface_name, SelectionPreference preference) {
   set_sel_prop_interface(&props->selection_properties, interface_name, preference);
}