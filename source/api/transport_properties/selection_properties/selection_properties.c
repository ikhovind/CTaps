#include "selection_properties.h"

#include <glib.h>
#include <logging/log.h>

#include "glibconfig.h"


void selection_properties_build(SelectionProperties* selection_properties) {
  memcpy(selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(SelectionProperties));
}

void set_sel_prop_preference(SelectionProperties* props, SelectionPropertyEnum prop_enum, SelectionPreference val) {
  if (props->selection_property[prop_enum].type != TYPE_PREFERENCE) {
    log_error("Type mismatch for property %s", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.simple_preference = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_direction(SelectionProperties* props, SelectionPropertyEnum prop_enum, DirectionOfCommunicationEnum val) {
  if (props->selection_property[prop_enum].type != TYPE_DIRECTION_ENUM) {
    log_error("Type mismatch for property %s", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.multipath_enum = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_multipath(SelectionProperties* props, SelectionPropertyEnum prop_enum, MultipathEnum val) {
  if (props->selection_property[prop_enum].type != TYPE_MULTIPATH_ENUM) {
    log_error("Type mismatch for property %s", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.multipath_enum = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_bool(SelectionProperties* props, SelectionPropertyEnum prop_enum, bool val) {
  if (props->selection_property[prop_enum].type != TYPE_BOOLEAN) {
    log_error("Type mismatch for property %s", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.boolean = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_interface(SelectionProperties* props, const char* interface_name, SelectionPreference preference) {
  log_debug("Setting interface preference: %s -> %d", interface_name, preference);
  // Check if the property has been initialized.
  if (props->selection_property[INTERFACE].value.preference_map == NULL) {
    log_trace("No existing interface map, creating a new one.");
    // This is an internal function to create the GHashTable.
    // It is not exposed in the header file.
    props->selection_property[INTERFACE].value.preference_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  }

  GHashTable* interface_map = (GHashTable*)props->selection_property[INTERFACE].value.preference_map;

  // Use g_strdup to create a new string to be owned by the hash table.
  g_hash_table_insert(interface_map, g_strdup(interface_name), GINT_TO_POINTER(preference));
}
