#include "protocol_registry.h"


// Function to add a new protocol to our list
void register_protocol(ProtocolImplementation* proto) {
    if (protocol_count < MAX_PROTOCOLS) {
        supported_protocols[protocol_count++] = proto;
    }
}

// Function to get the list of available protocols
const ProtocolImplementation** get_supported_protocols() {
    printf("Num Supported protocols is: %d\n", protocol_count);
    return (const ProtocolImplementation**)supported_protocols;
}
