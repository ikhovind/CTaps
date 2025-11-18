#ifndef PROTOCOL_REGISTRY_H
#define PROTOCOL_REGISTRY_H

#include "protocols/protocol_interface.h"

#define MAX_PROTOCOLS 256

// A dynamic list to hold registered protocols
static const ct_protocol_implementation_t* supported_protocols[MAX_PROTOCOLS] = {0};

void ct_register_protocol(ct_protocol_implementation_t* proto);

const ct_protocol_implementation_t** ct_get_supported_protocols();

size_t ct_get_num_protocols();

#endif  // PROTOCOL_REGISTRY_H
