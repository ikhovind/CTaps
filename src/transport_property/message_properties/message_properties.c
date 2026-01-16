#include "ctaps.h"
#include "ctaps_internal.h"

#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_message_properties_t* ct_message_properties_new(void) {
  ct_message_properties_t* props = malloc(sizeof(ct_message_properties_t));
  if (!props) {
    return NULL;
  }

  // Initialize with default values
  memcpy(props, &DEFAULT_MESSAGE_PROPERTIES, sizeof(ct_message_properties_t));
  return props;
}

ct_message_properties_t* ct_message_properties_deep_copy(const ct_message_properties_t* source) {
  if (!source) {
    return NULL;
  }

  ct_message_properties_t* copy = malloc(sizeof(ct_message_properties_t));
  if (!copy) {
    return NULL;
  }

  // Deep copy all properties
  memcpy(copy, source, sizeof(ct_message_properties_t));

  return copy;
}

void ct_message_properties_free(ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return;
  }
  free(message_properties);
}


void ct_message_properties_set_uint64(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, uint64_t value) {
  if (!message_properties) {
    return;
  }
  if (message_properties->message_property[property].type != TYPE_UINT64_MSG) {
    log_error("Type mismatch when setting message property %s", message_properties->message_property[property].name);
    return;
  }
  message_properties->message_property[property].value.uint64_value = value;
}

void ct_message_properties_set_uint32(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, uint32_t value) {
  if (!message_properties) {
    return;
  }
  if (message_properties->message_property[property].type != TYPE_UINT32_MSG) {
    log_error("Type mismatch when setting message property %s", message_properties->message_property[property].name);
    return;
  }
  message_properties->message_property[property].value.uint32_value = value;
}

void ct_message_properties_set_boolean(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, bool value) {
  if (!message_properties) {
    return;
  }
  if (message_properties->message_property[property].type != TYPE_BOOLEAN_MSG) {
    log_error("Type mismatch when setting message property %s", message_properties->message_property[property].name);
    return;
  }
  message_properties->message_property[property].value.boolean_value = value;
}

void ct_message_properties_set_capacity_profile(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, ct_capacity_profile_enum_t value) {
  if (!message_properties) {
    return;
  }
  if (message_properties->message_property[property].type != TYPE_ENUM_MSG) {
    log_error("Type mismatch when setting message property %s", message_properties->message_property[property].name);
    return;
  }
  message_properties->message_property[property].value.capacity_profile_enum_value = value;
}

uint64_t ct_message_properties_get_uint64(const ct_message_properties_t* message_properties, ct_message_properties_enum_t property) {
  if (!message_properties) {
    return 0;
  }
  if (message_properties->message_property[property].type != TYPE_UINT64_MSG) {
    log_error("Type mismatch when getting message property %s", message_properties->message_property[property].name);
    return 0;
  }
  return message_properties->message_property[property].value.uint64_value;
}

uint32_t ct_message_properties_get_uint32(const ct_message_properties_t* message_properties, ct_message_properties_enum_t property) {
  if (!message_properties) {
    return 0;
  }
  if (message_properties->message_property[property].type != TYPE_UINT32_MSG) {
    log_error("Type mismatch when getting message property %s", message_properties->message_property[property].name);
    return 0;
  }
  return message_properties->message_property[property].value.uint32_value;
}

bool ct_message_properties_get_boolean(const ct_message_properties_t* message_properties,  ct_message_properties_enum_t property) {
  if (!message_properties) {
    return false;
  }
  if (message_properties->message_property[property].type != TYPE_BOOLEAN_MSG) {
    log_error("Type mismatch when getting message property %s", message_properties->message_property[property].name);
    return false;
  }
  return message_properties->message_property[property].value.boolean_value;
}

ct_capacity_profile_enum_t ct_message_properties_get_capacity_profile(const ct_message_properties_t* message_properties,  ct_message_properties_enum_t property) {
  if (!message_properties) {
    return CAPACITY_PROFILE_BEST_EFFORT;
  }
  if (message_properties->message_property[property].type != TYPE_ENUM_MSG) {
    log_error("Type mismatch when getting message property %s", message_properties->message_property[property].name);
    return CAPACITY_PROFILE_BEST_EFFORT;
  }
  return message_properties->message_property[property].value.capacity_profile_enum_value;
}

bool ct_message_properties_is_final(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return false;
  }
  return message_properties->message_property[FINAL].value.boolean_value;
}
