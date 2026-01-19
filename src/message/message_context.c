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

  return ctx;
}

ct_message_context_t* ct_message_context_new_from_connection(const ct_connection_t* connection) {
  ct_message_context_t* ctx = ct_message_context_new();
  if (!ctx) {
    return NULL;
  }
  ctx->local_endpoint = &connection->local_endpoint;
  ctx->remote_endpoint = &connection->remote_endpoint;
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

  // shallow copy endpoints, because these are only set by the library, and are owned by an associated connection
  copy->local_endpoint = source->local_endpoint;
  copy->remote_endpoint = source->remote_endpoint;

  // Copy user context pointer (shallow copy - user owns the actual data)
  copy->user_receive_context = source->user_receive_context;

  return copy;
}

void ct_message_context_free(ct_message_context_t* message_context) {
  if (!message_context) {
    return;
  }

  free(message_context);
}

ct_message_properties_t* ct_message_context_get_message_properties(ct_message_context_t* message_context) {
  if (!message_context) {
    return NULL;
  }
  return &message_context->message_properties;
}

const ct_remote_endpoint_t* ct_message_context_get_remote_endpoint(const ct_message_context_t* message_context) {
  if (!message_context) {
    return NULL;
  }
  return message_context->remote_endpoint;
}

const ct_local_endpoint_t* ct_message_context_get_local_endpoint(const ct_message_context_t* message_context) {
  if (!message_context) {
    return NULL;
  }
  return message_context->local_endpoint;
}

void ct_message_context_set_uint64(ct_message_context_t* message_context, ct_message_properties_enum_t property, uint64_t value) {
  if (!message_context) {
    return;
  }
  ct_message_properties_set_uint64(&message_context->message_properties, property, value);
}

void ct_message_context_set_uint32(ct_message_context_t* message_context, ct_message_properties_enum_t property, uint32_t value) {
  if (!message_context) {
    return;
  }
  ct_message_properties_set_uint32(&message_context->message_properties, property, value);
}

void ct_message_context_set_boolean(ct_message_context_t* message_context, ct_message_properties_enum_t property, bool value) {
  if (!message_context) {
    return;
  }
  ct_message_properties_set_boolean(&message_context->message_properties, property, value);
}

void ct_message_context_set_capacity_profile(ct_message_context_t* message_context, ct_message_properties_enum_t property, ct_capacity_profile_enum_t value) {
  if (!message_context) {
    return;
  }
  ct_message_properties_set_capacity_profile(&message_context->message_properties, property, value);
}

uint64_t ct_message_context_get_uint64(const ct_message_context_t* message_context, ct_message_properties_enum_t property) {
  if (!message_context) {
    return 0;
  }
  return ct_message_properties_get_uint64(&message_context->message_properties, property);
}

uint32_t ct_message_context_get_uint32(const ct_message_context_t* message_context, ct_message_properties_enum_t property) {
  if (!message_context) {
    return 0;
  }
  return ct_message_properties_get_uint32(&message_context->message_properties, property);
}

bool ct_message_context_get_boolean(const ct_message_context_t* message_context, ct_message_properties_enum_t property) {
  if (!message_context) {
    return false;
  }
  return ct_message_properties_get_boolean(&message_context->message_properties, property);
}

ct_capacity_profile_enum_t ct_message_context_get_capacity_profile(const ct_message_context_t* message_context, ct_message_properties_enum_t property) {
  if (!message_context) {
    return CAPACITY_PROFILE_BEST_EFFORT;
  }
  return ct_message_properties_get_capacity_profile(&message_context->message_properties, property);
}
