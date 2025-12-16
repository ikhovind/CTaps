#include "ctaps.h"

#include <string.h>

void ct_message_properties_build(ct_message_properties_t* message_properties) {
  memcpy(message_properties, &DEFAULT_MESSAGE_PROPERTIES, sizeof(ct_message_properties_t));
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
}
