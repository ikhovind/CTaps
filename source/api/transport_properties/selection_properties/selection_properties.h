//
// Created by ikhovind on 12.08.25.
//

#ifndef SELECTION_PROPERTIES_H
#define SELECTION_PROPERTIES_H

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
  TYPE_INTERFACE_PREFERENCE,
} PropertyType;

typedef struct {
  char* name;   // e.g., "eth0", "en1", "Wi-Fi", "Cellular"
  SelectionPreference preference;    // Your existing Preference enum (Require, Prohibit, etc.)
} StringPreference;

typedef struct {
  size_t count;
  StringPreference* preferences;
} PreferenceSet;

// clang-format off
#define get_selection_property_list(f)                                                              \
  f(RELIABILITY, "reliability", TYPE_PREFERENCE)                               \
  f(PRESERVE_MSG_BOUNDARIES, "preserveMsgBoundaries", TYPE_PREFERENCE)         \
  f(PER_MSG_RELIABILITY, "perMsgReliability", TYPE_PREFERENCE)                 \
  f(PRESERVE_ORDER, "preserveOrder", TYPE_PREFERENCE)                          \
  f(ZERO_RTT_MSG, "zeroRttMsg", TYPE_PREFERENCE)                               \
  f(MULTISTREAMING, "multistreaming", TYPE_PREFERENCE)                         \
  f(FULL_CHECKSUM_SEND, "fullChecksumSend", TYPE_PREFERENCE)                   \
  f(FULL_CHECKSUM_RECV, "fullChecksumRecv", TYPE_PREFERENCE)                   \
  f(CONGESTION_CONTROL, "congestionControl", TYPE_PREFERENCE)                  \
  f(KEEP_ALIVE, "keepAlive", TYPE_PREFERENCE)                                  \
  f(interface, "interface", TYPE_INTERFACE_PREFERENCE)                         \
  f(PVD, "pvd", TYPE_PREFERENCE)                                               \
  f(USE_TEMPORARY_LOCAL_ADDRESS, "useTemporaryLocalAddress", TYPE_PREFERENCE)  \
  f(MULTIPATH, "multipath", TYPE_PREFERENCE)                                   \
  f(ADVERTISES_ALT_ADDRES, "advertisesAltAddr", TYPE_PREFERENCE)               \
  f(DIRECTION, "direction", TYPE_PREFERENCE)                                   \
  f(SOFT_ERROR_NOTIFY, "softErrorNotify", TYPE_PREFERENCE)                     \
  f(ACTIVE_READ_BEFORE_SEND, "activeReadBeforeSend", TYPE_PREFERENCE)
// clang-format on

#define output_enum(enum_name, string_name, property_type) enum_name,
#define output_arr(enum_name, string_name, property_type) {enum_name, string_name},

typedef enum { get_selection_property_list(output_enum) SELECTION_PROPERTY_END } SelectionProperty;

typedef struct {
  SelectionPreference preference[SELECTION_PROPERTY_END];
} SelectionProperties;

void selection_properties_init(SelectionProperties* selection_properties);
void selection_properties_set(SelectionProperties* selection_properties,
                              SelectionProperty selection_property,
                              SelectionPreference preference);
void selection_properties_require_prop(
    SelectionProperties* selection_properties,
    SelectionProperty selection_property);

void selection_properties_require_char(
    SelectionProperties* selection_properties, char* selection_property);

#define selection_properties_require(selection_properties, selection_property) \
  _Generic((selection_property),                                               \
      char*: selection_properties_require_char,                                \
      SelectionProperty: selection_properties_require_prop)(                   \
      selection_properties, selection_property)

#endif  // SELECTION_PROPERTIES_H
