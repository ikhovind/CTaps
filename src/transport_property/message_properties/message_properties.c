#include "ctaps.h"
#include "ctaps_internal.h"
#include "message_properties.h"

#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_message_properties_t* ct_message_properties_new(void) {
    ct_message_properties_t* props = malloc(sizeof(ct_message_properties_t));
    if (!props) {
        return NULL;
    }

    // Initialize with default values
    memcpy(props, &DEFAULT_MESSAGE_PROPERTIES, sizeof(ct_message_properties_t));
    return props;
}

ct_message_properties_t* ct_message_properties_deep_copy(const ct_message_properties_t* source) {
    if (!source) {
        return NULL;
    }

    ct_message_properties_t* copy = malloc(sizeof(ct_message_properties_t));
    if (!copy) {
        return NULL;
    }

    // Deep copy all properties
    memcpy(copy, source, sizeof(ct_message_properties_t));

    return copy;
}

void ct_message_properties_free(ct_message_properties_t* message_properties) {
    if (!message_properties) {
        return;
    }
    free(message_properties);
}

#define DEFINE_MSG_PROPERTY_GETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)                   \
    TYPE ct_message_properties_get_##FIELD(const ct_message_properties_t* msg_prop) {              \
        if (!msg_prop) {                                                                           \
            log_warn("Null pointer passed to get_" #FIELD);                                        \
            return (TYPE)(DEFAULT);                                                                \
        }                                                                                          \
        return msg_prop->list[ENUM].value.UNION_MEMBER_##TYPE_TAG;                                 \
    }

#define DEFINE_MSG_PROPERTY_SETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)                   \
    void ct_message_properties_set_##FIELD(ct_message_properties_t* msg_prop, TYPE val) {          \
        if (!msg_prop) {                                                                           \
            log_warn("Null pointer passed to set_" #FIELD);                                        \
            return;                                                                                \
        }                                                                                          \
        msg_prop->list[ENUM].value.UNION_MEMBER_##TYPE_TAG = val;                                  \
        msg_prop->list[ENUM].set_by_user = true;                                                   \
    }

get_message_property_list(DEFINE_MSG_PROPERTY_GETTER)
    get_message_property_list(DEFINE_MSG_PROPERTY_SETTER)
