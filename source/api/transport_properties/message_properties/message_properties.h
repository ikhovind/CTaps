//
// Created by ikhovind on 12.08.25.
//

#ifndef MESSAGE_PROPERTIES_H
#define MESSAGE_PROPERTIES_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <transport_properties/connection_properties/connection_properties.h>

typedef enum MessagePropertyType {
  TYPE_INTEGER_MSG,
  TYPE_BOOLEAN_MSG,
  TYPE_UINT64_MSG,
  TYPE_ENUM_MSG
} MessagePropertyType;

#define MSG_LIFETIME_INFINITE ULLONG_MAX
#define MSG_CHECKSUM_FULL_COVERAGE UINT32_MAX
#define DEFAULT_MSG_PRIORITY 100


typedef union {
  uint64_t uint64_value;
  uint32_t integer_value;
  bool boolean_value;
  CapacityProfileEnum enum_value;
} MessagePropertyValue;

typedef struct MessageProperty {
  char* name;
  MessagePropertyType type;
  bool set_by_user;
  MessagePropertyValue value;
} MessageProperty;

// clang-format off
#define get_message_property_list(f)                                                                    \
  f(MSG_LIFETIME,           "msgLifetime",          TYPE_UINT64_MSG,    MSG_LIFETIME_INFINITE)     \
  f(MSG_PRIORITY,           "msgPriority",          TYPE_INTEGER_MSG,   DEFAULT_MSG_PRIORITY)           \
  f(MSG_ORDERED,            "msgOrdered",           TYPE_BOOLEAN_MSG,   true)                      \
  f(SAFELY_REPLAYABLE,      "safelyReplayable",     TYPE_BOOLEAN_MSG,   false)                     \
  f(FINAL,                  "final",                TYPE_BOOLEAN_MSG,   false)                     \
  f(MSG_CHECKSUM_LEN,       "msgChecksumLen",       TYPE_INTEGER_MSG,   MSG_CHECKSUM_FULL_COVERAGE)     \
  f(MSG_RELIABLE,           "msgReliable",          TYPE_BOOLEAN_MSG,   true)                      \
  f(MSG_CAPACITY_PROFILE,   "msgCapacityProfile",   TYPE_ENUM_MSG,      CAPACITY_PROFILE_BEST_EFFORT)       \
  f(NO_FRAGMENTATION,       "noFragmentation",      TYPE_BOOLEAN_MSG,   false)                     \
  f(NO_SEGMENTATION,        "noSegmentation",       TYPE_BOOLEAN_MSG,   false)
// clang-format on

#define output_enum(enum_name, string_name, property_type, default_value) enum_name,

// Enum for all message properties
typedef enum { get_message_property_list(output_enum) MESSAGE_PROPERTY_END } MessagePropertyEnum;

typedef struct {
  MessageProperty message_property[MESSAGE_PROPERTY_END];
} MessageProperties;

// The value cast is a hack to please the C++ compiler used for our tests, has no effect on the actual data
#define create_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { (uint64_t)default_value }                                 \
},

// Create a single, read-only template with all the default values.
const static MessageProperties DEFAULT_MESSAGE_PROPERTIES = {
  .message_property = {
    get_message_property_list(create_property_initializer)
  }
};

// --- Function Prototypes (as seen in the other property headers) ---

void message_properties_init(MessageProperties* message_properties);

#endif  // MESSAGE_PROPERTIES_H
