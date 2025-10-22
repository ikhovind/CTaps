#ifndef TCP_H
#define TCP_H
#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct SocketManager;

int tcp_init(Connection* connection, const ConnectionCallbacks* connection_callbacks);
int tcp_close(const Connection* connection);
int tcp_send(Connection* connection, Message* message, MessageContext*);
int tcp_receive(Connection* connection, ReceiveCallbacks receive_callbacks);
int tcp_listen(struct SocketManager* socket_manager);
int tcp_stop_listen(struct SocketManager* listener);
int tcp_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer);

static ProtocolImplementation tcp_protocol_interface = {
    .name = "TCP",
    .selection_properties = {
      .selection_property = {
        get_selection_property_list(create_property_initializer)
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [MULTISTREAMING] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = REQUIRE}},
      }
    },
    .send = tcp_send,
    .init = tcp_init,
    .receive = tcp_receive,
    .close = tcp_close,
    .listen = tcp_listen,
    .stop_listen = tcp_stop_listen,
    .remote_endpoint_from_peer = tcp_remote_endpoint_from_peer
};

#endif //TCP_H
