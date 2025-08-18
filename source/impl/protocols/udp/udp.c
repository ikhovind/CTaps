#include "udp.h"

#include "protocols/registry/protocol_registry.h"

int udp_init() {

}

void register_udp_support() {
    register_protocol(&udp_protocol_interface);
}
