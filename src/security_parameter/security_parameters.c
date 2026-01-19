#include "ctaps.h"
#include "ctaps_internal.h"
#include "security_parameter/security_parameters.h"
#include "security_parameter/certificate_bundles/certificate_bundles.h"

#include <errno.h>
#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_security_parameters_t* ct_security_parameters_new(void) {
  ct_security_parameters_t* params = malloc(sizeof(ct_security_parameters_t));
  if (!params) {
    return NULL;
  }
  memcpy(params, &DEFAULT_SECURITY_PARAMETERS, sizeof(ct_security_parameters_t));
  return params;
}

static void ct_string_array_value_free(ct_string_array_value_t* arr) {
  if (!arr) {
    return;
  }
  for (size_t i = 0; i < arr->num_strings; i++) {
    free(arr->strings[i]);
  }
  free(arr->strings);
  free(arr);
}

void ct_sec_param_free(ct_security_parameters_t* security_parameters) {
  if (!security_parameters) {
    return;
  }
  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    ct_security_parameter_t* sec_param = &security_parameters->security_parameters[i];
    switch (sec_param->type) {
      case TYPE_STRING_ARRAY:
        ct_string_array_value_free(sec_param->value.array_of_strings);
        break;
      case TYPE_CERTIFICATE_BUNDLES:
        ct_certificate_bundles_free(sec_param->value.certificate_bundles);
        break;
    }
  }
  free(security_parameters);
}

static ct_string_array_value_t* ct_string_array_value_deep_copy(const ct_string_array_value_t* source) {
  if (!source) {
    return NULL;
  }

  ct_string_array_value_t* copy = malloc(sizeof(ct_string_array_value_t));
  if (!copy) {
    return NULL;
  }
  memset(copy, 0, sizeof(ct_string_array_value_t));

  if (source->num_strings == 0) {
    return copy;
  }

  copy->num_strings = source->num_strings;
  copy->strings = malloc(sizeof(char*) * source->num_strings);
  if (!copy->strings) {
    free(copy);
    return NULL;
  }

  for (size_t i = 0; i < source->num_strings; i++) {
    copy->strings[i] = strdup(source->strings[i]);
    if (!copy->strings[i]) {
      for (size_t j = 0; j < i; j++) {
        free(copy->strings[j]);
      }
      free(copy->strings);
      free(copy);
      return NULL;
    }
  }
  return copy;
}


ct_security_parameters_t* ct_security_parameters_deep_copy(const ct_security_parameters_t* source) {
  if (!source) {
    return NULL;
  }

  ct_security_parameters_t* copy = ct_security_parameters_new();
  if (!copy) {
    return NULL;
  }

  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    const ct_security_parameter_t* src_param = &source->security_parameters[i];
    ct_security_parameter_t* dst_param = &copy->security_parameters[i];

    dst_param->name = src_param->name;
    dst_param->type = src_param->type;
    dst_param->set_by_user = src_param->set_by_user;

    if (src_param->set_by_user) {
      switch (src_param->type) {
        case TYPE_STRING_ARRAY:
          dst_param->value.array_of_strings = ct_string_array_value_deep_copy(src_param->value.array_of_strings);
          break;
        case TYPE_CERTIFICATE_BUNDLES:
          dst_param->value.certificate_bundles = ct_certificate_bundles_deep_copy(src_param->value.certificate_bundles);
          break;
      }
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

  // Free existing value if set
  ct_string_array_value_free(sec_param->value.array_of_strings);

  ct_string_array_value_t* arr = malloc(sizeof(ct_string_array_value_t));
  if (!arr) {
    log_error("Failed to allocate memory for string array");
    return -ENOMEM;
  }

  arr->strings = malloc(sizeof(char*) * num_strings);
  if (!arr->strings) {
    log_error("Failed to allocate memory for string array strings");
    free(arr);
    return -ENOMEM;
  }

  for (size_t i = 0; i < num_strings; i++) {
    arr->strings[i] = strdup(strings[i]);
    if (!arr->strings[i]) {
      log_error("Failed to allocate memory for string array element");
      for (size_t j = 0; j < i; j++) {
        free(arr->strings[j]);
      }
      free(arr->strings);
      free(arr);
      return -ENOMEM;
    }
  }
  arr->num_strings = num_strings;

  sec_param->value.array_of_strings = arr;
  sec_param->set_by_user = true;
  return 0;
}

int ct_sec_param_set_property_certificate_bundles(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, ct_certificate_bundles_t* bundles) {
  if (property >= SEC_PROPERTY_END) {
    log_error("Attempted to set invalid security parameter property");
    return -EINVAL;
  }
  ct_security_parameter_t* sec_param = &security_parameters->security_parameters[property];
  if (sec_param->type != TYPE_CERTIFICATE_BUNDLES) {
    log_error("Attempted to set a non-certificate-bundle-array security parameter as certificate bundle array");
    return -EINVAL;
  }
  if (!bundles) {
    log_error("Passed NULL certificate bundles to set operation");
    return -EINVAL;
  }

  for (size_t i = 0; i < bundles->num_bundles; i++) {
    if (!bundles->certificate_bundles[i].certificate_file_name || !bundles->certificate_bundles[i].private_key_file_name) {
      log_error("Certificate bundle at index %zu is missing certificate or private key file name", i);
      return -EINVAL;
    }
  }

  // Free existing value if set
  ct_certificate_bundles_free(sec_param->value.certificate_bundles);

  sec_param->value.certificate_bundles = ct_certificate_bundles_deep_copy(bundles);
  if (!sec_param->value.certificate_bundles) {
    log_error("Failed to deep copy certificate bundles");
    return -ENOMEM;
  }

  sec_param->set_by_user = true;
  return 0;
}
