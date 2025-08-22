#ifndef UDP_H
#define UDP_H

#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"

int udp_init(Connection* connection,
             int (*init_done_cb)(Connection* connection));
int udp_close(const Connection* connection);
void udp_connect(void);
void register_udp_support();
int udp_send(Connection* connection, Message* message);
int udp_receive(Connection* connection,
                int (*receive_msg_cb)(struct Connection* connection,
                                      Message** received_message));

const static ProtocolImplementation udp_protocol_interface = {
    .name = "UDP",
    .features = {.values = {[RELIABILITY] = PROHIBIT,
                            [PRESERVE_ORDER] = PROHIBIT,
                            [CONGESTION_CONTROL] = PROHIBIT,
                            [PRESERVE_MSG_BOUNDARIES] = REQUIRE}},
    .send = udp_send,
    .init = udp_init,
    .receive = udp_receive,
    .close = udp_close,
};

#endif  // UDP_H
