#include "ctaps.h"
#include "ctaps_internal.h"
#include "message_context.h"
#include "endpoint/local_endpoint.h"
#include "endpoint/remote_endpoint.h"

#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_message_context_t* ct_message_context_new(void) {
  ct_message_context_t* ctx = malloc(sizeof(ct_message_context_t));
  if (!ctx) {
    return NULL;
  }

  memset(ctx, 0, sizeof(ct_message_context_t));

  // Initialize with default message properties
  ctx->message_properties = DEFAULT_MESSAGE_PROPERTIES;
  ctx->local_endpoint = NULL;
  ctx->remote_endpoint = NULL;
  ctx->user_receive_context = NULL;

  return ctx;
}

ct_message_context_t* ct_message_context_deep_copy(const ct_message_context_t* source) {
  if (!source) {
    return NULL;
  }

  ct_message_context_t* copy = malloc(sizeof(ct_message_context_t));
  if (!copy) {
    return NULL;
  }

  // Deep copy message properties
  copy->message_properties = source->message_properties;

  // Deep copy local endpoint if present
  if (source->local_endpoint) {
    copy->local_endpoint = local_endpoint_copy(source->local_endpoint);
    if (!copy->local_endpoint) {
      free(copy);
      return NULL;
    }
  } else {
    copy->local_endpoint = NULL;
  }

  // Deep copy remote endpoint if present
  if (source->remote_endpoint) {
    copy->remote_endpoint = remote_endpoint_copy(source->remote_endpoint);
    if (!copy->remote_endpoint) {
      if (copy->local_endpoint) {
        ct_local_endpoint_free(copy->local_endpoint);
      }
      free(copy);
      return NULL;
    }
  } else {
    copy->remote_endpoint = NULL;
  }

  // Copy user context pointer (shallow copy - user owns the actual data)
  copy->user_receive_context = source->user_receive_context;

  return copy;
}

void ct_message_context_free(ct_message_context_t* message_context) {
  if (!message_context) {
    return;
  }

  // Free endpoints if they exist
  if (message_context->local_endpoint) {
    ct_local_endpoint_free(message_context->local_endpoint);
  }

  if (message_context->remote_endpoint) {
    ct_remote_endpoint_free(message_context->remote_endpoint);
  }

  free(message_context);
}

ct_message_properties_t* ct_message_context_get_message_properties(ct_message_context_t* message_context) {
  if (!message_context) {
    return NULL;
  }
  return &message_context->message_properties;
}
