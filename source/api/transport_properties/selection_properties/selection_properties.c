#include "selection_properties.h"

#define create_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { default_value }                                             \
  },

// Create a single, read-only template with all the default values.
static const SelectionProperties DEFAULT_SELECTION_PROPERTIES = {
  .selection_property = {
    get_selection_property_list(create_property_initializer)
  }
};


void selection_properties_init(SelectionProperties* selection_properties) {
  memcpy(selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(SelectionProperties));
}

void set_sel_prop_preference(SelectionProperties* props, SelectionPropertyEnum prop_enum, SelectionPreference val) {
  if (props->selection_property[prop_enum].type != TYPE_PREFERENCE) {
    fprintf(stderr, "Type mismatch for property %s\n", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.simple_preference = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_direction(SelectionProperties* props, SelectionPropertyEnum prop_enum, DirectionOfCommunicationEnum val) {
  if (props->selection_property[prop_enum].type != TYPE_DIRECTION_ENUM) {
    fprintf(stderr, "Type mismatch for property %s\n", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.multipath_enum = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_multipath(SelectionProperties* props, SelectionPropertyEnum prop_enum, MultipathEnum val) {
  if (props->selection_property[prop_enum].type != TYPE_MULTIPATH_ENUM) {
    fprintf(stderr, "Type mismatch for property %s\n", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.multipath_enum = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop_bool(SelectionProperties* props, SelectionPropertyEnum prop_enum, bool val) {
  if (props->selection_property[prop_enum].type != TYPE_BOOLEAN) {
    fprintf(stderr, "Type mismatch for property %s\n", props->selection_property[prop_enum].name);
    return;
  }
  props->selection_property[prop_enum].value.boolean = val;
  props->selection_property[prop_enum].set_by_user = true;
}

void set_sel_prop(SelectionProperties* props, SelectionPropertyEnum prop_enum, SelectionPropertyValue val) {
  props->selection_property[prop_enum].value = val;
  props->selection_property[prop_enum].set_by_user = true;
}
