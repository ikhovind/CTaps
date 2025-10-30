#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum SecPropertyType {
  TYPE_STRING_ARRAY,
} SecPropertyType;

typedef struct {
  char** strings;
  size_t num_strings;
} StringArrayValue;

typedef union {
  StringArrayValue array_of_strings;
} SecPropertyValue;

typedef struct SecProperty {
  char* name;
  SecPropertyType type;
  bool set_by_user;
  SecPropertyValue value;
} SecurityParameter;

// clang-format off
#define get_security_parameter_list(f)                                                    \
  f(ALPN,                 "alpn",                TYPE_STRING_ARRAY,           NULL)        \
// clang-format on

#define output_enum(enum_name, string_name, property_type, default_value) enum_name,

// Enum for all message properties
typedef enum { get_security_parameter_list(output_enum) SEC_PROPERTY_END } SecurityPropertyEnum;

typedef struct {
  SecurityParameter security_parameters[SEC_PROPERTY_END];
} SecurityParameters;

// The value cast is a hack to please the C++ compiler used for our tests, has no effect on the actual data
#define create_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { default_value }                                 \
},

// Create a single, read-only template with all the default values.
const static SecurityParameters DEFAULT_SEC_PROPERTIES = {
  .security_parameters = {
    get_security_parameter_list(create_property_initializer)
  }
};

void security_parameters_build(SecurityParameters* security_parameters);

int sec_param_set_property_string_array(SecurityParameters* security_parameters, SecurityPropertyEnum property, char** strings, size_t num_strings);

void free_security_parameter_content(SecurityParameters* security_parameters);
