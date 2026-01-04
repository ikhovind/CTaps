#include "ctaps.h"
#include "ctaps_internal.h"

#include <errno.h>
#include <logging/log.h>
#include <stdint.h>
#include <string.h>


void ct_connection_properties_build(ct_connection_properties_t* properties) {
  memcpy(properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ct_connection_properties_t));
}

int ct_cp_set_prop_uint32(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const uint32_t val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.uint32_val = val;
  return 0;
}
int ct_cp_set_prop_uint64(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const uint64_t val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.uint64_val = val;
  return 0;
}
int ct_cp_set_prop_bool(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const bool val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.bool_val = val;
  return 0;
}
int ct_cp_set_prop_enum(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const int val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.enum_val = val;
  return 0;
}
