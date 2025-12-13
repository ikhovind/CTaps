#include "ctaps.h"

// Message Properties
/**
 * @brief Initialize message properties with default values.
 * @param[out] message_properties Structure to initialize
 */
void ct_message_properties_build(ct_message_properties_t* message_properties) {
  memcpy(message_properties, &DEFAULT_MESSAGE_PROPERTIES, sizeof(ct_message_properties_t));
}

/**
 * @brief Check if the FINAL property is set in message properties.
 * @param[out] message_properties Structure to check 
 *
 * @return true if FINAL property is set, false otherwise or null
 */
bool ct_message_properties_is_final(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return false;
  }
  return message_properties->message_property[FINAL].value.boolean_value;
}

void ct_message_properties_set_final(ct_message_properties_t* message_properties) {
  message_properties->message_property[FINAL].value.boolean_value = true;
}
