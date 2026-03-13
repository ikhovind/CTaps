#include "ctaps.h"
#include "ctaps_internal.h"
#include "message_context.h"
#include "transport_property/message_properties/message_properties.h"

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
    ctx->local_endpoint = ct_connection_get_active_local_endpoint(connection);
    ctx->remote_endpoint = ct_connection_get_active_remote_endpoint(connection);
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

const ct_message_properties_t*
ct_message_context_get_message_properties(const ct_message_context_t* message_context) {
    if (!message_context) {
        return NULL;
    }
    return &message_context->message_properties;
}

const ct_remote_endpoint_t*
ct_message_context_get_remote_endpoint(const ct_message_context_t* message_context) {
    if (!message_context) {
        return NULL;
    }
    return message_context->remote_endpoint;
}

const ct_local_endpoint_t*
ct_message_context_get_local_endpoint(const ct_message_context_t* message_context) {
    if (!message_context) {
        return NULL;
    }
    return message_context->local_endpoint;
}

void* ct_message_context_get_receive_context(const ct_message_context_t* message_context) {
    if (!message_context) {
        return NULL;
    }
    return message_context->user_receive_context;
}

#define DEFINE_MSG_CONTEXT_PROPERTY_GETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)           \
    TYPE ct_message_context_get_##FIELD(const ct_message_context_t* ctx) {                         \
        if (!ctx) {                                                                                \
            log_warn("Null pointer passed to get_" #FIELD);                                        \
            return (TYPE)(DEFAULT);                                                                \
        }                                                                                          \
        return ctx->message_properties.list[ENUM].value.UNION_MEMBER_##TYPE_TAG;                   \
    }

#define DEFINE_MSG_CONTEXT_PROPERTY_SETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)           \
    void ct_message_context_set_##FIELD(ct_message_context_t* ctx, TYPE val) {                     \
        if (!ctx) {                                                                                \
            log_warn("Null pointer passed to set_" #FIELD);                                        \
            return;                                                                                \
        }                                                                                          \
        ctx->message_properties.list[ENUM].value.UNION_MEMBER_##TYPE_TAG = val;                    \
        ctx->message_properties.list[ENUM].set_by_user = true;                                     \
    }

get_message_property_list(DEFINE_MSG_CONTEXT_PROPERTY_GETTER)
    get_message_property_list(DEFINE_MSG_CONTEXT_PROPERTY_SETTER)
