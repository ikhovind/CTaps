//
// Created by ikhovind on 12.08.25.
//

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
} SelectionPreference;

typedef enum PropertyType {
  TYPE_PREFERENCE,
  TYPE_PREFERENCE_SET,
  TYPE_MULTIPATH_ENUM,
  TYPE_BOOLEAN,
  TYPE_DIRECTION_ENUM
} PropertyType;

typedef enum DirectionOfCommunication {
  DIRECTION_BIDIRECTIONAL,
  DIRECTION_UNIDIRECTIONAL_SEND,
  DIRECTION_UNIDIRECTIONAL_RECV
} DirectionOfCommunicationEnum;

typedef enum MultipathEnum {
  MULTIPATH_DISABLED,
  MULTIPATH_ACTIVE,
  MULTIPATH_PASSIVE
} MultipathEnum;


typedef struct {
  char* name;   // e.g., "eth0", "en1", "Wi-Fi", "Cellular"
  SelectionPreference preference;    // Your existing Preference enum (Require, Prohibit, etc.)
} StringPreference;

typedef struct {
  size_t count;
  StringPreference* preferences;
} PreferenceSet;

typedef union {
  SelectionPreference simple_preference;
  PreferenceSet preference_set;
  MultipathEnum multipath_enum;
  bool boolean;
  DirectionOfCommunicationEnum direction_enum;
} SelectionPropertyValue;

typedef struct SelectionProperty {
  char* name;
  PropertyType type;
  // needed since default values vary by connection type
  // but the user is able to set properties before we know the connection type
  bool set_by_user;
  SelectionPropertyValue value;
} SelectionProperty;

#define EMPTY_PREFERENCE_SET_DEFAULT 0
#define RUNTIME_DEPENDENT_DEFAULT 0

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
  f(ADVERTISES_ALT_ADDRES,       "advertisesAltAddr",          TYPE_BOOLEAN,              false)  \
  f(DIRECTION,                   "direction",                  TYPE_DIRECTION_ENUM,       DIRECTION_BIDIRECTIONAL)  \
  f(SOFT_ERROR_NOTIFY,           "softErrorNotify",            TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(ACTIVE_READ_BEFORE_SEND,     "activeReadBeforeSend",       TYPE_PREFERENCE,           NO_PREFERENCE)
// clang-format on

#define output_enum(enum_name, string_name, property_type, default_value) enum_name,

typedef enum { get_selection_property_list(output_enum) SELECTION_PROPERTY_END } SelectionPropertyEnum;

typedef struct {
  SelectionProperty selection_property[SELECTION_PROPERTY_END];
} SelectionProperties;

void selection_properties_init(SelectionProperties* selection_properties);

void set_sel_prop_preference(SelectionProperties* props, SelectionPropertyEnum prop_enum, SelectionPreference val);

void set_sel_prop_multipath(SelectionProperties* props, SelectionPropertyEnum prop_enum, MultipathEnum val);

void set_sel_prop_direction(SelectionProperties* props, SelectionPropertyEnum prop_enum, DirectionOfCommunicationEnum val);

void set_sel_prop_bool(SelectionProperties* props, SelectionPropertyEnum prop_enum, bool val);

void set_sel_prop(SelectionProperties* props, SelectionPropertyEnum prop_enum, SelectionPropertyValue);

#endif  // SELECTION_PROPERTIES_H
