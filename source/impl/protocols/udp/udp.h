#ifndef UDP_H
#define UDP_H

#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"

int udp_init(Connection* connection, InitDoneCb init_done_cb);
int udp_close(const Connection* connection);
void register_udp_support();
int udp_send(Connection* connection, Message* message);
int udp_receive(Connection* connection, ReceiveMessageRequest receive_message_cb);
int udp_listen(struct Listener* listener);
int udp_stop_listen(struct Listener* listener);

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
    .listen = udp_listen,
    .stop_listen = udp_stop_listen
};

#endif  // UDP_H
