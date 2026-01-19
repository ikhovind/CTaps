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
  // Initialize with default values including proper type fields from X-macro
  memcpy(params, &DEFAULT_SECURITY_PARAMETERS, sizeof(ct_security_parameters_t));
  return params;
}

void ct_sec_param_free(ct_security_parameters_t* security_parameters) {
  if (!security_parameters) {
    return;
  }
  // free content
  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    ct_security_parameter_t* sec_param = &security_parameters->security_parameters[i];
    if (sec_param->type == TYPE_STRING_ARRAY) {
      for (size_t j = 0; j < sec_param->value.array_of_strings.num_strings; j++) {
        free(sec_param->value.array_of_strings.strings[j]);
      }
      free((void*)sec_param->value.array_of_strings.strings);
    }
    if (sec_param->type == TYPE_CERTIFICATE_BUNDLES) {
      ct_certificate_bundles_t* bundles = &sec_param->value.certificate_bundles;
      for (size_t i = 0; i < bundles->num_bundles; i++) {
        free(bundles->certificate_bundles[i].certificate_file_name);
        free(bundles->certificate_bundles[i].private_key_file_name);
      }
      free(bundles->certificate_bundles);
    }
  }
  free(security_parameters);
}

ct_string_array_value_t copy_array_of_strings_content(const ct_string_array_value_t* source) {
  ct_string_array_value_t copy = {0};
  if (!source || source->num_strings == 0) {
    return copy;
  }

  copy.num_strings = source->num_strings;
  copy.strings = malloc(sizeof(char*) * source->num_strings);
  if (!copy.strings) {
    copy.num_strings = 0;
    return copy;
  }

  for (size_t i = 0; i < source->num_strings; i++) {
    copy.strings[i] = strdup(source->strings[i]);
    if (!copy.strings[i]) {
      // Free what we've allocated so far
      for (size_t j = 0; j < i; j++) {
        free(copy.strings[j]);
      }
      free(copy.strings);
      copy.strings = NULL;
      copy.num_strings = 0;
      return copy;
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

  // Copy each security parameter
  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    const ct_security_parameter_t* src_param = &source->security_parameters[i];
    ct_security_parameter_t* dst_param = &copy->security_parameters[i];

    dst_param->name = src_param->name;  // name is a string literal, no need to copy
    dst_param->type = src_param->type;
    dst_param->set_by_user = src_param->set_by_user;

    if (src_param->set_by_user) {
      switch (src_param->type) {
        case TYPE_STRING_ARRAY:
          dst_param->value.array_of_strings = copy_array_of_strings_content(&src_param->value.array_of_strings);
          break;
        case TYPE_CERTIFICATE_BUNDLES:
          dst_param->value.certificate_bundles = ct_certificate_bundles_copy_content(&src_param->value.certificate_bundles);
          break;
        default:
          log_error("Unsupported security parameter type for deep copy: %d", src_param->type);
          ct_sec_param_free(copy);
          return NULL;
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

int ct_sec_param_set_property_certificate_bundles(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, ct_certificate_bundles_t* bundles) {
  if (property >= SEC_PROPERTY_END) {
    log_error("Attempted to set invalid security parameter property");
    return -EINVAL;
  }
  if (security_parameters->security_parameters[property].type != TYPE_CERTIFICATE_BUNDLES) {
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

  security_parameters->security_parameters[property].value.certificate_bundles.certificate_bundles = malloc(sizeof(ct_certificate_bundle_t) * bundles->num_bundles);
  if (!security_parameters->security_parameters[property].value.certificate_bundles.certificate_bundles) {
    log_error("Failed to allocate memory for certificate bundles when performing set operation");
    return -ENOMEM;
  }

  security_parameters->security_parameters[property].value.certificate_bundles = ct_certificate_bundles_copy_content(bundles);

  security_parameters->security_parameters[property].value.certificate_bundles.num_bundles = bundles->num_bundles;
  security_parameters->security_parameters[property].set_by_user = true;
  return 0;
}
