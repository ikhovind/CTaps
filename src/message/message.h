#ifndef CT_MESSAGE_H
#define CT_MESSAGE_H

#include "ctaps.h"
#include "ctaps_internal.h"

ct_queued_message_t* ct_queued_message_new(ct_message_t* message, ct_message_context_t* context);

void ct_queued_message_free_ctaps_ownership(ct_queued_message_t* queued_message);

void ct_queued_message_free_all(ct_queued_message_t* queued_message);

// Internal utility function for deep copying messages
// Returns NULL on allocation failure
ct_message_t* ct_message_deep_copy(const ct_message_t* message);

#endif // CT_MESSAGE_H
