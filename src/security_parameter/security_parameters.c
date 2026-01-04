#include "ctaps.h"
#include "ctaps_internal.h"

#include <errno.h>
#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_security_parameters_t* ct_security_parameters_new(void) {
  ct_security_parameters_t* params = malloc(sizeof(ct_security_parameters_t));
  if (!params) {
    return NULL;
  }
  // TODO - maybe introduce default values here instead?
  memset(params, 0, sizeof(ct_security_parameters_t));
  return params;
}

void ct_security_parameters_free(ct_security_parameters_t* security_parameters) {
  if (!security_parameters) {
    return;
  }
  ct_free_security_parameter_content(security_parameters);
  free(security_parameters);
}

ct_security_parameters_t* ct_security_parameters_deep_copy(const ct_security_parameters_t* source) {
  if (!source) {
    return NULL;
  }

  ct_security_parameters_t* copy = ct_security_parameters_new();
  if (!copy) {
    return NULL;
  }

  // Copy each security parameter
  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    const ct_security_parameter_t* src_param = &source->security_parameters[i];
    ct_security_parameter_t* dst_param = &copy->security_parameters[i];

    dst_param->name = src_param->name;  // name is a string literal, no need to copy
    dst_param->type = src_param->type;
    dst_param->set_by_user = src_param->set_by_user;

    // Deep copy the value based on type
    if (src_param->type == TYPE_STRING_ARRAY && src_param->set_by_user) {
      size_t num_strings = src_param->value.array_of_strings.num_strings;
      char** strings = malloc(sizeof(char*) * num_strings);
      if (!strings) {
        ct_security_parameters_free(copy);
        return NULL;
      }

      for (size_t j = 0; j < num_strings; j++) {
        strings[j] = strdup(src_param->value.array_of_strings.strings[j]);
        if (!strings[j]) {
          // Free what we've allocated so far
          for (size_t k = 0; k < j; k++) {
            free(strings[k]);
          }
          free(strings);
          ct_security_parameters_free(copy);
          return NULL;
        }
      }

      dst_param->value.array_of_strings.strings = strings;
      dst_param->value.array_of_strings.num_strings = num_strings;
    }
  }

  return copy;
}

int ct_sec_param_set_property_string_array(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, char** strings, size_t num_strings) {
  if (property >= SEC_PROPERTY_END) {
    log_error("Attempted to set invalid security parameter property");
    return -EINVAL;
  }
  ct_security_parameter_t* sec_param = &security_parameters->security_parameters[property];
  if (sec_param->type != TYPE_STRING_ARRAY) {
    log_error("Attempted to set a non-string-array security parameter as string array");
    return -EINVAL;
  }

  security_parameters->security_parameters[property].value.array_of_strings.strings = malloc(sizeof(char*) * num_strings);
  if (security_parameters->security_parameters[property].value.array_of_strings.strings == NULL) {
    log_error("Failed to allocate memory for string array");
    return -ENOMEM;
  }

  for (size_t i = 0; i < num_strings; i++) {
    security_parameters->security_parameters[property].value.array_of_strings.strings[i] = strdup(strings[i]);
    if (security_parameters->security_parameters[property].value.array_of_strings.strings[i] == NULL) {
      log_error("Failed to allocate memory for string array element");
      for (size_t j = 0; j < i; j++) {
        free(security_parameters->security_parameters[property].value.array_of_strings.strings[j]);
      }
      return -ENOMEM;
    }
  }
  security_parameters->security_parameters[property].value.array_of_strings.num_strings = num_strings;
  sec_param->set_by_user = true;
  return 0;
}

void ct_free_security_parameter_content(ct_security_parameters_t* security_parameters) {
  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    ct_security_parameter_t* sec_param = &security_parameters->security_parameters[i];
    if (sec_param->type == TYPE_STRING_ARRAY) {
      for (size_t j = 0; j < sec_param->value.array_of_strings.num_strings; j++) {
        free(sec_param->value.array_of_strings.strings[j]);
      }
      free((void*)sec_param->value.array_of_strings.strings);
    }
  }
}
