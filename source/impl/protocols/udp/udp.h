#ifndef UDP_H
#define UDP_H

#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct SocketManager;

int udp_init(Connection* connection, const ConnectionCallbacks* connection_callbacks);
int udp_close(const Connection* connection);
int udp_send(Connection* connection, Message* message, MessageContext*);
int udp_receive(Connection* connection, ReceiveCallbacks receive_callbacks);
int udp_listen(struct SocketManager* socket_manager);
int udp_stop_listen(struct SocketManager* listener);
int udp_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer);


static ProtocolImplementation udp_protocol_interface = {
    .name = "UDP",
    .selection_properties = {
      .selection_property = {
        get_selection_property_list(create_property_initializer)
        [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
      }
    },
    .send = udp_send,
    .init = udp_init,
    .receive = udp_receive,
    .close = udp_close,
    .listen = udp_listen,
    .stop_listen = udp_stop_listen,
    .remote_endpoint_from_peer = udp_remote_endpoint_from_peer
};

#endif  // UDP_H
