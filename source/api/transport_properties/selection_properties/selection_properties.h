#ifndef SELECTION_PROPERTIES_H
#define SELECTION_PROPERTIES_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  PROHIBIT = -2,
  AVOID,
  NO_PREFERENCE,
  PREFER,
  REQUIRE,
} ct_selection_preference_t;

typedef enum ct_property_type_t {
  TYPE_PREFERENCE,
  TYPE_PREFERENCE_SET,
  TYPE_MULTIPATH_ENUM,
  TYPE_BOOLEAN,
  TYPE_DIRECTION_ENUM
} ct_property_type_t;

typedef enum ct_direction_of_communication_t {
  DIRECTION_BIDIRECTIONAL,
  DIRECTION_UNIDIRECTIONAL_SEND,
  DIRECTION_UNIDIRECTIONAL_RECV
} ct_direction_of_communication_enum_t;

typedef enum ct_multipath_enum_t {
  MULTIPATH_DISABLED,
  MULTIPATH_ACTIVE,
  MULTIPATH_PASSIVE
} ct_multipath_enum_t;

typedef union {
  ct_selection_preference_t simple_preference;
  void* preference_map;
  ct_multipath_enum_t multipath_enum;
  bool boolean;
  ct_direction_of_communication_enum_t direction_enum;
} ct_selection_property_value_t;

typedef struct ct_selection_property_t {
  char* name;
  ct_property_type_t type;
  // needed since default values vary by connection type
  // but the user is able to set properties before we know the connection type
  bool set_by_user;
  ct_selection_property_value_t value;
} ct_selection_property_t;

#define EMPTY_PREFERENCE_SET_DEFAULT NO_PREFERENCE
#define RUNTIME_DEPENDENT_DEFAULT NO_PREFERENCE

// clang-format off
#define get_selection_property_list(f)                                                                    \
  f(RELIABILITY,                 "reliability",                TYPE_PREFERENCE,           REQUIRE)        \
  f(PRESERVE_MSG_BOUNDARIES,     "preserveMsgBoundaries",      TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(PER_MSG_RELIABILITY,         "perMsgReliability",          TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(PRESERVE_ORDER,              "preserveOrder",              TYPE_PREFERENCE,           REQUIRE)        \
  f(ZERO_RTT_MSG,                "zeroRttMsg",                 TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(MULTISTREAMING,              "multistreaming",             TYPE_PREFERENCE,           PREFER)         \
  f(FULL_CHECKSUM_SEND,          "fullChecksumSend",           TYPE_PREFERENCE,           REQUIRE)        \
  f(FULL_CHECKSUM_RECV,          "fullChecksumRecv",           TYPE_PREFERENCE,           REQUIRE)        \
  f(CONGESTION_CONTROL,          "congestionControl",          TYPE_PREFERENCE,           REQUIRE)        \
  f(KEEP_ALIVE,                  "keepAlive",                  TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(INTERFACE,                   "interface",                  TYPE_PREFERENCE_SET,       EMPTY_PREFERENCE_SET_DEFAULT)  \
  f(PVD,                         "pvd",                        TYPE_PREFERENCE_SET,       EMPTY_PREFERENCE_SET_DEFAULT)  \
  f(USE_TEMPORARY_LOCAL_ADDRESS, "useTemporaryLocalAddress",   TYPE_PREFERENCE,           RUNTIME_DEPENDENT_DEFAULT)         \
  f(MULTIPATH,                   "multipath",                  TYPE_MULTIPATH_ENUM,       RUNTIME_DEPENDENT_DEFAULT)  \
  f(ADVERTISES_ALT_ADDRES,       "advertisesAltAddr",          TYPE_BOOLEAN,              NO_PREFERENCE)  \
  f(DIRECTION,                   "direction",                  TYPE_DIRECTION_ENUM,       DIRECTION_BIDIRECTIONAL)  \
  f(SOFT_ERROR_NOTIFY,           "softErrorNotify",            TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(ACTIVE_READ_BEFORE_SEND,     "activeReadBeforeSend",       TYPE_PREFERENCE,           NO_PREFERENCE)
// clang-format on

#define output_enum(enum_name, string_name, property_type, default_value) enum_name,

typedef enum { get_selection_property_list(output_enum) SELECTION_PROPERTY_END } ct_selection_property_enum_t;

typedef struct {
  ct_selection_property_t selection_property[SELECTION_PROPERTY_END];
} ct_selection_properties_t;

// The value cast is a hack to please the c++ compiler for our tests
#define create_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { (ct_selection_preference_t)default_value }                     \
},

const static ct_selection_properties_t DEFAULT_SELECTION_PROPERTIES = {
  .selection_property = {
    get_selection_property_list(create_property_initializer)
  }
};

void ct_selection_properties_build(ct_selection_properties_t* selection_properties);

void ct_set_sel_prop_preference(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val);

void ct_set_sel_prop_interface(ct_selection_properties_t* props, const char* interface_name, ct_selection_preference_t preference);

void ct_set_sel_prop_multipath(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val);

void ct_set_sel_prop_direction(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val);

void ct_set_sel_prop_bool(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, bool val);

#endif  // SELECTION_PROPERTIES_H
