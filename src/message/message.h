#ifndef MESSAGE_H
#define MESSAGE_H

#include "ctaps.h"

// Internal utility function for deep copying messages
// Returns NULL on allocation failure
ct_message_t* ct_message_deep_copy(const ct_message_t* message);

#endif // MESSAGE_H
