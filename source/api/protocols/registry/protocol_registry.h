#ifndef PROTOCOL_REGISTRY_H
#define PROTOCOL_REGISTRY_H

#include "protocols/protocol_interface.h"

#define MAX_PROTOCOLS 256

// A dynamic list to hold registered protocols
static const ProtocolImplementation* supported_protocols[MAX_PROTOCOLS] = {0};

void register_protocol(ProtocolImplementation* proto);

const ProtocolImplementation** get_supported_protocols();

size_t get_num_protocols();

#endif  // PROTOCOL_REGISTRY_H
