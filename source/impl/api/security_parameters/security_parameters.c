#include "ctaps.h"
#include "logging/log.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

void ct_security_parameters_build(ct_security_parameters_t* security_parameters) {
  // TODO - maybe introduce default values here instead?
  memset(security_parameters, 0, sizeof(ct_security_parameters_t));
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
