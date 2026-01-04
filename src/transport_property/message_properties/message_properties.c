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

bool ct_message_properties_is_final(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return false;
  }
  return message_properties->message_property[FINAL].value.boolean_value;
}

void ct_message_properties_set_final(ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return;
  }
  message_properties->message_property[FINAL].value.boolean_value = true;
  message_properties->message_property[FINAL].set_by_user = true;
}
