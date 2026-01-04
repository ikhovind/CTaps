#include "ctaps.h"
#include "ctaps_internal.h"
#include <stdbool.h>
#include <stdlib.h>
#include "transport_property/selection_properties/selection_properties.h"
#include "transport_property/transport_properties.h"
#include "transport_property/connection_properties/connection_properties.h"

// Internal cleanup function declaration (from selection_properties.c)
void ct_selection_properties_cleanup(ct_selection_properties_t* selection_properties);

// Internal deep copy function declaration (from selection_properties.c)
void ct_selection_properties_deep_copy(ct_selection_properties_t* dest, const ct_selection_properties_t* src);

ct_transport_properties_t* ct_transport_properties_new(void) {
  ct_transport_properties_t* props = malloc(sizeof(ct_transport_properties_t));
  if (!props) {
    return NULL;
  }

  // Initialize with default values (inlined from removed _build function)
  memcpy(&props->selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(ct_selection_properties_t));
  memcpy(&props->connection_properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ct_connection_properties_t));

  return props;
}

void ct_transport_properties_free(ct_transport_properties_t* props) {
  if (!props) {
    return;
  }

  // Clean up selection properties (frees GHashTable if created)
  ct_selection_properties_cleanup(&props->selection_properties);

  // connection_properties has no dynamic allocations, no cleanup needed

  free(props);
}

ct_transport_properties_t* ct_transport_properties_deep_copy(const ct_transport_properties_t* src) {
  if (!src) {
    return NULL;
  }

  ct_transport_properties_t* dest = ct_transport_properties_new();
  if (!dest) {
    return NULL;
  }

  // Deep copy selection properties (handles GHashTable properly)
  ct_selection_properties_deep_copy(&dest->selection_properties, &src->selection_properties);

  // Shallow copy connection properties (no dynamic allocations)
  dest->connection_properties = src->connection_properties;

  return dest;
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
