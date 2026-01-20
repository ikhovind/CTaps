#include "ctaps.h"
#include "ctaps_internal.h"
#include "message/message.h"

#include "message/message_context.h"

#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_queued_message_t* ct_queued_message_new(ct_message_t* message, ct_message_context_t* context) {
  if (!message) {
    log_error("Cannot create queued message with NULL message");
    return NULL;
  }
  ct_queued_message_t* queued_message = malloc(sizeof(ct_queued_message_t));
  if (!queued_message) {
    log_error("Failed to allocate memory for queued message");
    return NULL;
  }
  queued_message->message = message;
  queued_message->context = context;
  return queued_message;
}

void ct_queued_message_free_ctaps_ownership(ct_queued_message_t* queued_message) {
  if (!queued_message) {
    return;
  }
  ct_message_context_free(queued_message->context);
  free(queued_message);
}

void ct_queued_message_free_all(ct_queued_message_t* queued_message) {
  if (!queued_message) {
    return;
  }
  ct_message_free(queued_message->message);
  ct_message_context_free(queued_message->context);
  free(queued_message);
}

void ct_message_free(ct_message_t* message) {
  if (!message) {
    return;
  }
  log_trace("Freeing message of size %zu", message->length);
  free(message->content);
  free(message);
}

ct_message_t* ct_message_deep_copy(const ct_message_t* message) {
  if (!message) {
    log_error("Cannot deep copy a NULL message");
    return NULL;
  }

  ct_message_t* copy = malloc(sizeof(ct_message_t));
  if (!copy) {
    log_error("Failed to allocate memory for message copy");
    return NULL;
  }
  log_trace("Deep copying message of size %zu", message->length);

  copy->length = message->length;
  copy->content = malloc(message->length);
  if (!copy->content) {
    log_error("Failed to allocate memory for message content copy");
    free(copy);
    return NULL;
  }

  memcpy(copy->content, message->content, message->length);
  return copy;
}

ct_message_t* ct_message_new(void) {
  ct_message_t* message = malloc(sizeof(ct_message_t));
  if (!message) {
    return NULL;
  }
  memset(message, 0, sizeof(ct_message_t));
  return message;
}

ct_message_t* ct_message_new_with_content(const char* content, size_t length) {
  ct_message_t* message = ct_message_new();
  if (!message) {
    return NULL;
  }
  message->content = malloc(length);
  if (!message->content) {
    log_error("Failed to allocate memory for message content");
    free(message);
    return NULL;
  }
  message->length = length;
  memcpy(message->content, content, length);
  return message;
}

size_t ct_message_get_length(const ct_message_t* message) {
  return message ? message->length : 0;
}

const char* ct_message_get_content(const ct_message_t* message) {
  return message ? message->content : NULL;
}

void ct_message_set_content(ct_message_t* message, const char* content, size_t length) {
  if (!message) {
    log_error("NULL message provided to ct_message_set_content");
    return;
  }
  if ((void*)content == (void*)message->content) {
    log_debug("New content is the same as existing content; no action taken");
    return;
  }

  if (message->content) {
    log_debug("Replacing existing message content of size %zu", message->length);
    free(message->content);
    message->content = NULL;
  }
  if (!content || length == 0) {
    log_debug("Setting message content to NULL due to NULL content or zero length");
    message->content = NULL;
    message->length = 0;
    return;
  }


  message->content = malloc(length);
  if (!message->content) {
    log_error("Failed to allocate memory for message content");
    message->length = 0;
    return;
  }
  memcpy(message->content, content, length);
  message->length = length;
}
