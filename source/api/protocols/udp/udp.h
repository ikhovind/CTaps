#ifndef UDP_H
#define UDP_H

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"



int udp_init(void);
void udp_connect(void);
void register_udp();

static ProtocolImplementation udp_protocol_interface = {
    .name = "UDP",
    .features = {
        .values = {
            [RELIABILITY]               = PROHIBIT,
            [PRESERVE_ORDER]            = PROHIBIT,
            [CONGESTION_CONTROL]        = PROHIBIT,
            [PRESERVE_MSG_BOUNDARIES]   = REQUIRE
        }
    },
    .init = udp_init,
};

#endif //UDP_H
