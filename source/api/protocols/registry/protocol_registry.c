#include "protocol_registry.h"
#include <stddef.h>

static int protocol_count = 0;
// Function to add a new protocol to our list
void register_protocol(ProtocolImplementation* proto) {
  if (protocol_count < MAX_PROTOCOLS) {
    supported_protocols[protocol_count++] = proto;
  }
}

const ProtocolImplementation** get_supported_protocols() {
  return supported_protocols;
}

size_t get_num_protocols() {
  return protocol_count;
}