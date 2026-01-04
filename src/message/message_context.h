#ifndef CT_MESSAGE_CONTEXT_H
#define CT_MESSAGE_CONTEXT_H

#include "ctaps.h"

/**
 * @brief Deep copy message context.
 *
 * Creates a new message context object with copies of all data, including
 * deep copies of local and remote endpoints if present. The user_receive_context
 * pointer is shallow copied - the user owns the actual data.
 *
 * @param source Source message context to copy
 * @return Newly allocated copy, or NULL on failure
 */
ct_message_context_t* ct_message_context_deep_copy(const ct_message_context_t* source);

#endif  // CT_MESSAGE_CONTEXT_H
