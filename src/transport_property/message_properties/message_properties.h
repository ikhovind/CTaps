#ifndef CT_MESSAGE_PROPERTIES_H
#define CT_MESSAGE_PROPERTIES_H

#include <ctaps.h>

/**
 * @brief Deep copy message properties.
 *
 * Creates a new message properties object with copies of all data.
 *
 * @param source Source message properties to copy
 * @return Newly allocated copy, or NULL on failure
 */
ct_message_properties_t* ct_message_properties_deep_copy(const ct_message_properties_t* source);

#endif  // CT_MESSAGE_PROPERTIES_H
