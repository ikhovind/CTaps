#ifndef UDP_H
#define UDP_H

#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"



int udp_init(Connection *connection);
int udp_close(void);
void udp_connect(void);
void register_udp_support();
int udp_send(struct Connection* connection, Message* message);
int udp_receive(struct Connection* connection, Message* message);

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
    .send = udp_send,
    .init = udp_init,
    .receive = udp_receive,
};

#endif //UDP_H
