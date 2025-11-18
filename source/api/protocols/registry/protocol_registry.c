#include "ctaps.h"

#include <stddef.h>

static int protocol_count = 0;
// Function to add a new protocol to our list
void ct_register_protocol(ct_protocol_implementation_t* proto) {
  if (protocol_count < MAX_PROTOCOLS) {
    ct_supported_protocols[protocol_count++] = proto;
  }
}

const ct_protocol_implementation_t** ct_get_supported_protocols() {
  return ct_supported_protocols;
}

size_t ct_get_num_protocols() {
  return protocol_count;
}
