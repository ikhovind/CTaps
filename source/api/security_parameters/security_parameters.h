#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum ct_sec_property_type_t {
  TYPE_STRING_ARRAY,
} ct_sec_property_type_t;

typedef struct {
  char** strings;
  size_t num_strings;
} ct_string_array_value_t;

typedef union {
  ct_string_array_value_t array_of_strings;
} ct_sec_property_value_t;

typedef struct ct_sec_property_t {
  char* name;
  ct_sec_property_type_t type;
  bool set_by_user;
  ct_sec_property_value_t value;
} ct_security_parameter_t;

// clang-format off
#define get_security_parameter_list(f)                                                    \
  f(ALPN,                 "alpn",                TYPE_STRING_ARRAY,           NULL)        \
// clang-format on

#define output_enum(enum_name, string_name, property_type, default_value) enum_name,

// Enum for all message properties
typedef enum { get_security_parameter_list(output_enum) SEC_PROPERTY_END } ct_security_property_enum_t;

typedef struct {
  ct_security_parameter_t security_parameters[SEC_PROPERTY_END];
} ct_security_parameters_t;

// The value cast is a hack to please the C++ compiler used for our tests, has no effect on the actual data
#define create_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { default_value }                                 \
},

// Create a single, read-only template with all the default values.
const static ct_security_parameters_t DEFAULT_SEC_PROPERTIES = {
  .security_parameters = {
    get_security_parameter_list(create_property_initializer)
  }
};

void ct_security_parameters_build(ct_security_parameters_t* security_parameters);

int ct_sec_param_set_property_string_array(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, char** strings, size_t num_strings);

void ct_free_security_parameter_content(ct_security_parameters_t* security_parameters);
