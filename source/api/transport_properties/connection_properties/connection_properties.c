#include "connection_properties.h"

#include <errno.h>
#include <logging/log.h>
#include <stdint.h>
#include <string.h>


void connection_properties_build(ConnectionProperties* properties) {
  memcpy(properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ConnectionProperties));
}

int cp_set_prop_uint32(ConnectionProperties* props, const ConnectionPropertyEnum prop_enum, const uint32_t val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.uint32_val = val;
  return 0;
}
int cp_set_prop_uint64(ConnectionProperties* props, const ConnectionPropertyEnum prop_enum, const uint64_t val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.uint64_val = val;
  return 0;
}
int cp_set_prop_bool(ConnectionProperties* props, const ConnectionPropertyEnum prop_enum, const bool val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.bool_val = val;
  return 0;
}
int cp_set_prop_enum(ConnectionProperties* props, const ConnectionPropertyEnum prop_enum, const int val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.enum_val = val;
  return 0;
}