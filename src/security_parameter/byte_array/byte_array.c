#include "ctaps.h"
#include "byte_array.h"
#include "ctaps_internal.h"
#include "logging/log.h"

ct_byte_array_t* ct_byte_array_new_from_data(const uint8_t* data, size_t length) {
  ct_byte_array_t* byte_array = malloc(sizeof(ct_byte_array_t));
  if (!byte_array) {
    log_error("Failed to allocate memory for byte array");
    return NULL;
  }
  byte_array->bytes = malloc(length);
  if (!byte_array->bytes) {
    log_error("Failed to allocate memory for byte array data");
    free(byte_array);
    return NULL;
  }
  byte_array->length = length;
  memcpy(byte_array->bytes, data, length);
  return byte_array;
}

void ct_byte_array_free(ct_byte_array_t* byte_array) {
  if (!byte_array) {
    return;
  }
  free(byte_array->bytes);
  byte_array->bytes = NULL;
  free(byte_array);
}

ct_byte_array_t* ct_byte_array_copy(const ct_byte_array_t* source) {
  log_trace("Copying byte array of length %zu", source ? source->length : 0);
  if (!source) {
    return NULL;
  }
  ct_byte_array_t* copy = ct_byte_array_new_from_data(source->bytes, source->length);
  if (!copy) {
    log_error("Failed to copy byte array");
    return NULL;
  }
  return copy;
}
