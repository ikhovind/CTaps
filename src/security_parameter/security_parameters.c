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
  memset(params, 0, sizeof(ct_security_parameters_t));
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
      case TYPE_STRING:
        free(sec_param->value.string);
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
          if (!dst_param->value.array_of_strings) {
            log_error("Failed to deep copy string array security parameter");
            ct_sec_param_free(copy);
            return NULL;
          }
          break;
        case TYPE_CERTIFICATE_BUNDLES:
          dst_param->value.certificate_bundles = ct_certificate_bundles_deep_copy(src_param->value.certificate_bundles);
          if (!dst_param->value.certificate_bundles) {
            log_error("Failed to deep copy certificate bundles security parameter");
            ct_sec_param_free(copy);
            return NULL;
          }
          break;
        case TYPE_STRING:
          if (src_param->value.string) {
            dst_param->value.string = strdup(src_param->value.string);
            if (!dst_param->value.string) {
              log_error("Failed to deep copy string security parameter");
              ct_sec_param_free(copy);
              return NULL;
            }
          } else {
            dst_param->value.string = NULL;
          }
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

int ct_sec_param_set_ticket_store_path(ct_security_parameters_t* security_parameters, const char* ticket_store_path) {
  if (!security_parameters) {
    log_error("Attempted to set ticket store path on NULL security parameters");
    return -EINVAL;
  }
  if (security_parameters->security_parameters[TICKET_STORE_PATH].value.string) {
    log_trace("Freeing existing ticket store path before setting new value");
    free(security_parameters->security_parameters[TICKET_STORE_PATH].value.string);
    security_parameters->security_parameters[TICKET_STORE_PATH].value.string = NULL;
  }

  security_parameters->security_parameters[TICKET_STORE_PATH].set_by_user = true;
  if (!ticket_store_path) {
    log_debug("Setting ticket store path to NULL, clearing existing value if any");
    security_parameters->security_parameters[TICKET_STORE_PATH].value.string = NULL;
    return 0;
  }
  security_parameters->security_parameters[TICKET_STORE_PATH].value.string = strdup(ticket_store_path);
  if (!security_parameters->security_parameters[TICKET_STORE_PATH].value.string) {
    log_error("Failed to allocate memory for ticket store path");
    return -ENOMEM;
  }

  return 0;
}

const char* ct_sec_param_get_ticket_store_path(const ct_security_parameters_t* security_parameters) {
  if (!security_parameters) {
    log_error("Attempted to get ticket store path from NULL security parameters");
    return NULL;
  }
  return security_parameters->security_parameters[TICKET_STORE_PATH].value.string;
}

ct_string_array_value_t* ct_string_array_value_new(char** strings, size_t num_strings) {
  ct_string_array_value_t* arr = malloc(sizeof(ct_string_array_value_t));
  if (!arr) {
    log_error("Failed to allocate memory for ct_string_array_value_t");
    return NULL;
  }
  memset(arr, 0, sizeof(ct_string_array_value_t));
  arr->strings = malloc(sizeof(char*) * num_strings);
  arr->num_strings = num_strings;
  for (size_t i = 0; i < num_strings; i++) {
    arr->strings[i] = strdup(strings[i]);
    if (!arr->strings[i]) {
      log_error("Failed to allocate memory for string array element");
      for (size_t j = 0; j < i; j++) {
        free(arr->strings[j]);
      }
      free(arr->strings);
      free(arr);
      return NULL;
    }
  }
  return arr;
}

const char** ct_sec_param_get_alpn_strings(const ct_security_parameters_t* security_parameters, size_t* out_num_strings) {
  if (!security_parameters || !out_num_strings) {
    log_error("Invalid arguments to get ALPN strings");
    return NULL;
  }
  if (!security_parameters->security_parameters[ALPN].value.array_of_strings) {
    log_trace("No ALPN strings set in security parameters");
    *out_num_strings = 0;
    return NULL;
  }
  log_trace("Fetching %zu ALPN strings from security parameters", security_parameters->security_parameters[ALPN].value.array_of_strings->num_strings);
  const ct_security_parameter_t* sec_param = &security_parameters->security_parameters[ALPN];
  *out_num_strings = security_parameters->security_parameters[ALPN].value.array_of_strings->num_strings;
  return (const char**) sec_param->value.array_of_strings->strings;
}
