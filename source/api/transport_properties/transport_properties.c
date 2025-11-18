#include <stdbool.h>

#include "transport_properties.h"

#include "connection_properties/connection_properties.h"
#include "protocols/protocol_interface.h"
#include "selection_properties/selection_properties.h"

void ct_transport_properties_build(ct_transport_properties_t* properties) {
  ct_selection_properties_build(&properties->selection_properties);
  ct_connection_properties_build(&properties->connection_properties);
}

void ct_tp_set_sel_prop_preference(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val) {
  ct_set_sel_prop_preference(&props->selection_properties, prop_enum, val);  // Call the actual function to
}

void ct_tp_set_sel_prop_multipath(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val) {
  ct_set_sel_prop_multipath(&props->selection_properties, prop_enum, val);
}

void ct_tp_set_sel_prop_direction(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val) {
  ct_set_sel_prop_direction(&props->selection_properties, prop_enum, val);
}

void ct_tp_set_sel_prop_bool(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, bool val) {
  ct_set_sel_prop_bool(&props->selection_properties, prop_enum, val);
}

void ct_tp_set_sel_prop_interface(ct_transport_properties_t* props, char* interface_name, ct_selection_preference_t preference) {
   ct_set_sel_prop_interface(&props->selection_properties, interface_name, preference);
}