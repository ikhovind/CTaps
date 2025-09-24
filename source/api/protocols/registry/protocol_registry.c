#include "protocol_registry.h"

static int protocol_count = 0;
// Function to add a new protocol to our list
void register_protocol(ProtocolImplementation* proto) {
  if (protocol_count < MAX_PROTOCOLS) {
    supported_protocols[protocol_count++] = proto;
  }
}

const ProtocolImplementation** get_supported_protocols() {
  printf("Num Supported protocols is: %d\n", protocol_count);
  return supported_protocols;
}

size_t get_num_protocols() {
  return protocol_count;
}