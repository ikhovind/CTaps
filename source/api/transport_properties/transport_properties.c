#include <stdbool.h>

#include "transport_properties.h"

#include "connection_properties/connection_properties.h"
#include "protocols/protocol_interface.h"
#include "selection_properties/selection_properties.h"

void transport_properties_build(TransportProperties* properties) {
  selection_properties_build(&properties->selection_properties);
  connection_properties_build(&properties->connection_properties);
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