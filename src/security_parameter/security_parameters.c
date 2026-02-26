#include "ctaps.h"
#include "ctaps_internal.h"
#include "security_parameter/byte_array/byte_array.h"
#include "security_parameter/certificate_bundles/certificate_bundles.h"
#include "security_parameter/security_parameters.h"

#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_security_parameters_t* ct_security_parameters_new(void) {
  ct_security_parameters_t* params = malloc(sizeof(ct_security_parameters_t));
  if (!params) {
    log_error("Failed to allocate memory for ct_security_parameters_t");
    return NULL;
  }
  memset(params, 0, sizeof(ct_security_parameters_t));
  memcpy(params, &DEFAULT_SECURITY_PARAMETERS, sizeof(ct_security_parameters_t));
  return params;
}

static void ct_string_array_value_free(ct_string_array_t arr) {
  log_debug("Freeing string array with %zu strings", arr.num_strings);
  for (size_t i = 0; i < arr.num_strings; i++) {
    log_debug("String %zu: %s", i, arr.strings[i]);
    free(arr.strings[i]);
  }
  free(arr.strings);
}

void ct_security_parameters_free(ct_security_parameters_t* security_parameters) {
  log_trace("Freeing security parameters at address %p", (void*)security_parameters);
  if (!security_parameters) {
    return;
  }
  for (size_t i = 0; i < SEC_PROPERTY_END; i++) {
    ct_security_parameter_t* sec_param = &security_parameters->list[i];
    switch (sec_param->type) {
      case TYPE_STRING_ARRAY:
        log_debug("String array type name is %s", sec_param->name);
        ct_string_array_value_free(sec_param->value.array_of_strings);
        break;
      case TYPE_CERTIFICATE_BUNDLES:
        ct_certificate_bundles_free(sec_param->value.certificate_bundles);
        break;
      case TYPE_STRING:
        free(sec_param->value.string);
        break;
      case TYPE_BYTE_ARRAY:
        ct_byte_array_free(sec_param->value.byte_array);
        break;
      default:
        break;
    }
  }
  free(security_parameters);
}

static int ct_string_array_value_deep_copy(const ct_string_array_t source, ct_string_array_t *dest) {

  *dest = source;

  if (source.num_strings == 0) {
    log_debug("Source string array is empty, setting destination to empty as well");
    return 0;
  }
  log_debug("Source string array has %zu strings, performing deep copy", source.num_strings);

  dest->strings = malloc(sizeof(char*) * source.num_strings);
  if (!dest->strings) {
    log_error("Failed to allocate memory for string array copy");
    return -ENOMEM;
  }
  for (size_t i = 0; i < source.num_strings; i++) {
    log_debug("Copying string %zu: %s", i, source.strings[i]);
    dest->strings[i] = strdup(source.strings[i]);
    if (!dest->strings[i]) {
      log_error("Failed to allocate memory for string array element copy");
      for (size_t j = 0; j < i; j++) {
        free(dest->strings[j]);
      }
      free(dest->strings);
      memset(dest, 0, sizeof(ct_string_array_t));
      return -ENOMEM;
    }
  }
  return 0;
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
    const ct_security_parameter_t* src_param = &source->list[i];
    ct_security_parameter_t* dst_param = &copy->list[i];

    dst_param->name = src_param->name;
    dst_param->type = src_param->type;
    dst_param->set_by_user = src_param->set_by_user;

    if (src_param->set_by_user) {
      switch (src_param->type) {
        case TYPE_STRING_ARRAY: {
          log_debug("Deep copying string array security parameter: %s", src_param->name);
          int rc = ct_string_array_value_deep_copy(src_param->value.array_of_strings, &dst_param->value.array_of_strings);
          if (rc < 0) {
            log_error("Failed to deep copy string array security parameter");
            ct_security_parameters_free(copy);
            return NULL;
          }
          break;
        }
        case TYPE_CERTIFICATE_BUNDLES:
          dst_param->value.certificate_bundles = ct_certificate_bundles_deep_copy(src_param->value.certificate_bundles);
          if (!dst_param->value.certificate_bundles.certificate_bundles) {
            log_error("Failed to deep copy certificate bundles security parameter");
            ct_security_parameters_free(copy);
            return NULL;
          }
          break;
        case TYPE_STRING:
          if (src_param->value.string) {
            dst_param->value.string = strdup(src_param->value.string);
            if (!dst_param->value.string) {
              log_error("Failed to deep copy string security parameter");
              ct_security_parameters_free(copy);
              return NULL;
            }
          } else {
            dst_param->value.string = NULL;
          }
          break;
        case TYPE_BYTE_ARRAY:
          if (src_param->value.byte_array.length == 0) {
            break;
          }
          dst_param->value.byte_array.bytes = malloc(src_param->value.byte_array.length);
          if (!dst_param->value.byte_array.bytes) {
            log_error("Failed to deep copy byte array security parameter");
            ct_security_parameters_free(copy);
            return NULL;
          }
          memcpy(dst_param->value.byte_array.bytes, src_param->value.byte_array.bytes, src_param->value.byte_array.length);
          dst_param->value.byte_array.length = src_param->value.byte_array.length;
          break;
        default:
         break;
      }
    }
  }

  return copy;
}

int ct_security_parameters_set_ticket_store_path(ct_security_parameters_t* sec, const char* ticket_store_path) {
  if (!sec) {
    log_error("Attempted to set ticket store path on NULL security parameters");
    return -EINVAL;
  }
  if (sec->list[TICKET_STORE_PATH].value.string) {
    log_trace("Freeing existing ticket store path before setting new value");
    free(sec->list[TICKET_STORE_PATH].value.string);
    sec->list[TICKET_STORE_PATH].value.string = NULL;
  }

  sec->list[TICKET_STORE_PATH].set_by_user = true;
  if (!ticket_store_path) {
    log_debug("Setting ticket store path to NULL, clearing existing value if any");
    sec->list[TICKET_STORE_PATH].value.string = NULL;
    return 0;
  }
  sec->list[TICKET_STORE_PATH].value.string = strdup(ticket_store_path);
  if (!sec->list[TICKET_STORE_PATH].value.string) {
    log_error("Failed to allocate memory for ticket store path");
    return -ENOMEM;
  }

  return 0;
}

const char* ct_security_parameters_get_ticket_store_path(const ct_security_parameters_t* sec) {
  if (!sec) {
    log_error("Attempted to get ticket store path from NULL security parameters");
    return NULL;
  }
  return sec->list[TICKET_STORE_PATH].value.string;
}

ct_string_array_t* ct_string_array_value_new(char** strings, size_t num_strings) {
  ct_string_array_t* arr = malloc(sizeof(ct_string_array_t));
  if (!arr) {
    log_error("Failed to allocate memory for ct_string_array_value_t");
    return NULL;
  }
  memset(arr, 0, sizeof(ct_string_array_t));
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

int ct_security_parameters_clear_alpn(ct_security_parameters_t* sec) {
  if (!sec) {
    log_warn("Attempted to clear alpn on NULL security parameters");
    return -EINVAL;
  }
  for (size_t i = 0; i < sec->list[ALPN].value.array_of_strings.num_strings; i++) {
    free(sec->list[ALPN].value.array_of_strings.strings[i]);
  }
  if (sec->list[ALPN].value.array_of_strings.strings) {
    free(sec->list[ALPN].value.array_of_strings.strings);
  }
  sec->list[ALPN].value.array_of_strings.strings = NULL;
  sec->list[ALPN].value.array_of_strings.num_strings = 0;
  return 0;
}

int ct_security_parameters_add_alpn(ct_security_parameters_t* sec, const char* alpn) {
  if (!sec || !alpn) {
    log_warn("Attempted to add alpn with NULL parameters");
    log_debug("Security parameters: %p, alpn: %p", sec, alpn);
    return -EINVAL;
  }
  ct_string_array_t prev_alpns = sec->list[ALPN].value.array_of_strings;

  char** new_strings = realloc(prev_alpns.strings, (prev_alpns.num_strings + 1) * sizeof(char*));
  if (!new_strings) {
    log_error("Could not allocate memory for server certificate array");
    return -ENOMEM;
  }

  new_strings[prev_alpns.num_strings] = strdup(alpn);
  if (!new_strings[prev_alpns.num_strings]) {
    log_error("Could not allocate memory for server certificate file");
    free(new_strings);
    return -ENOMEM;
  }

  sec->list[ALPN].value.array_of_strings.strings = new_strings;
  sec->list[ALPN].value.array_of_strings.num_strings++;
  sec->list[ALPN].set_by_user = true;
  return 0;
}

const char** ct_security_parameters_get_alpns(const ct_security_parameters_t* sec, size_t* num_alpns) {
  if (!sec || !num_alpns) {
    log_error("Invalid arguments to get ALPNs");
    return NULL;
  }
  if (sec->list[ALPN].value.array_of_strings.num_strings == 0) {
    log_trace("No ALPN strings set in security parameters");
    *num_alpns = 0;
    return NULL;
  }
  const ct_security_parameter_t* sec_param = &sec->list[ALPN];
  *num_alpns = sec->list[ALPN].value.array_of_strings.num_strings;
  return (const char**) sec_param->value.array_of_strings.strings;
}

const uint8_t* ct_security_parameters_get_session_ticket_encryption_key(const ct_security_parameters_t* sec, size_t* key_len) {
  if (!sec || !key_len) {
    log_error("Invalid security parameters argument to get session ticket encryption key");
    return NULL;
  }
  *key_len = sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.length;
  return sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes;
}

int ct_security_parameters_set_session_ticket_encryption_key(ct_security_parameters_t* sec, const uint8_t* key, size_t key_len) {
  if (!sec) {
    log_error("Invalid security parameters argument to set session ticket encryption key");
    return -EINVAL;
  }
  if (sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.length != 0) {
    free(sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes);
    sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes = NULL;
    sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.length = 0;
  }
  if (key_len > 0) {
    sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes = malloc(key_len);
    memset(sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes, 0, key_len);
    memcpy(sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes, key, key_len);
    sec->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.length = key_len;
  }
  sec->list[SESSION_TICKET_ENCRYPTION_KEY].set_by_user = true;
  return 0;
}

int ct_security_parameters_add_certificate(ct_security_parameters_t* sec, ct_security_property_enum_t type ,const char* cert_file, const char* key_file) {
  if (!sec || !cert_file) {
    log_warn("Attempted to set certificate with NULL parameters");
    log_debug("Security parameters: %p, key_file: %p", sec, key_file);
    return -EINVAL;
  }
  ct_certificate_bundles_t prev_bundles = sec->list[type].value.certificate_bundles;

  ct_certificate_bundle_t* bundle_array = realloc(prev_bundles.certificate_bundles, (prev_bundles.num_bundles + 1) * sizeof(ct_certificate_bundle_t));
  if (!bundle_array) {
    log_error("Could not allocate memory for server certificate array");
    return -ENOMEM;
  }

  ct_certificate_bundle_t* new_bundle = &bundle_array[prev_bundles.num_bundles];
  new_bundle->certificate_file_name = strdup(cert_file);
  if (!new_bundle->certificate_file_name) {
    log_error("Could not allocate memory for server certificate file");
    free(bundle_array);
    return -ENOMEM;
  }
  if (key_file) {
    new_bundle->private_key_file_name = strdup(key_file);
    if (!new_bundle->private_key_file_name) {
      log_error("Could not allocate memory for server certificate key file");
      free(new_bundle->certificate_file_name);
      free(bundle_array);
      return -ENOMEM;
    }
  }

  sec->list[type].value.certificate_bundles.certificate_bundles = bundle_array;
  sec->list[type].value.certificate_bundles.num_bundles++;
  sec->list[type].set_by_user = true;
  return 0;
}

int ct_security_parameters_add_server_certificate(ct_security_parameters_t* sec, const char* cert_file, const char* key_file) {
  return ct_security_parameters_add_certificate(sec, SERVER_CERTIFICATE, cert_file, key_file);
}

int ct_security_parameters_add_client_certificate(ct_security_parameters_t* sec, const char* cert_file, const char* key_file) {
  return ct_security_parameters_add_certificate(sec, CLIENT_CERTIFICATE, cert_file, key_file);
}

const char* ct_security_parameters_get_certificate_file(const ct_security_parameters_t* sec, ct_security_property_enum_t type, size_t index) {
  if (!sec) {
    log_warn("Attempting to get certificate file from NULL security parameters");
    return NULL;
  }
  if (sec->list[type].value.certificate_bundles.num_bundles < index) {
    log_warn("Certificate file with index %llu does not exist", index);
    return NULL;
  }
  return sec->list[type].value.certificate_bundles.certificate_bundles[index].certificate_file_name;
}

const char* ct_security_parameters_get_key_file(const ct_security_parameters_t* sec, ct_security_property_enum_t type, size_t index) {
  if (!sec) {
    log_warn("Attempting to get key file from NULL security parameters");
    return NULL;
  }
  if (sec->list[type].value.certificate_bundles.num_bundles < index) {
    log_warn("Key file with index %llu does not exist", index);
    return NULL;
  }
  return sec->list[type].value.certificate_bundles.certificate_bundles[index].private_key_file_name;
}

size_t ct_security_parameters_get_server_certificate_count(const ct_security_parameters_t* sec) {
  if (!sec) {
    log_warn("Attempting to get server certificate count from NULL security parameters");
    return 0;
  }
  return sec->list[SERVER_CERTIFICATE].value.certificate_bundles.num_bundles;
}

const char* ct_security_parameters_get_server_certificate_file(const ct_security_parameters_t* sec, size_t index) {
  return ct_security_parameters_get_certificate_file(sec, SERVER_CERTIFICATE, index);
}

const char* ct_security_parameters_get_server_certificate_key_file(const ct_security_parameters_t* sec, size_t index) {
  return ct_security_parameters_get_key_file(sec, SERVER_CERTIFICATE, index);
}

size_t ct_security_parameters_get_client_certificate_count(const ct_security_parameters_t* sec) {
  return sec->list[CLIENT_CERTIFICATE].value.certificate_bundles.num_bundles;
}

const char* ct_security_parameters_get_client_certificate_file(const ct_security_parameters_t* sec, size_t index) {
  return ct_security_parameters_get_certificate_file(sec, CLIENT_CERTIFICATE, index);
}
const char* ct_security_parameters_get_client_certificate_key_file(const ct_security_parameters_t* sec, size_t index) {
  return ct_security_parameters_get_key_file(sec, CLIENT_CERTIFICATE, index);
}

int ct_security_parameters_set_server_name_identification(ct_security_parameters_t* security_parameters, const char* sni) {
  if (!security_parameters) {
    log_error("Attempted to set server name identification on NULL security parameters");
    return -EINVAL;
  }
  if (security_parameters->list[SERVER_NAME_IDENTIFICATION].value.string) {
    log_trace("Freeing existing server name identification before setting new value");
    free(security_parameters->list[SERVER_NAME_IDENTIFICATION].value.string);
  }
  security_parameters->list[SERVER_NAME_IDENTIFICATION].set_by_user = true;
  if (sni) {
    security_parameters->list[SERVER_NAME_IDENTIFICATION].value.string = strdup(sni);
  }
  else {
    security_parameters->list[SERVER_NAME_IDENTIFICATION].value.string = NULL;
  }
  return 0;
}

int ct_security_parameters_add_supported_group(ct_security_parameters_t* sec, const char* group) {
  if (!sec) {
    log_warn("Attempted to add supported group to NULL security parameters");
    return -EINVAL;
  }

  if (sec->list[SUPPORTED_GROUP].value.string) {
    log_trace("Freeing existing server name identification before setting new value");
    free(sec->list[SUPPORTED_GROUP].value.string);
  }
  sec->list[SUPPORTED_GROUP].set_by_user = true;
  if (group) {
    sec->list[SUPPORTED_GROUP].value.string = strdup(group);
    if (!sec->list[SUPPORTED_GROUP].value.string) {
      return -EINVAL;
    }
  }
  else {
    sec->list[SUPPORTED_GROUP].value.string = NULL;
  }
  return 0;
}

const char* ct_security_parameters_get_server_name_identification(const ct_security_parameters_t* sec) {
  if (!sec) {
    log_error("Attempted to get server name identification from NULL security parameters");
    return NULL;
  }
  return sec->list[SERVER_NAME_IDENTIFICATION].value.string;
}
