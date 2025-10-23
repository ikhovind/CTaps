#ifndef QUIC_H
#define QUIC_H

#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct SocketManager;

int quic_init(Connection* connection, const ConnectionCallbacks* connection_callbacks);
int quic_close(const Connection* connection);
int quic_send(Connection* connection, Message* message, MessageContext*);
int quic_receive(Connection* connection, ReceiveCallbacks receive_callbacks);
int quic_listen(struct SocketManager* socket_manager);
int quic_stop_listen(struct SocketManager* listener);
int quic_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer);

static ProtocolImplementation quic_protocol_interface = {
    .name = "QUIC",
    .selection_properties = {
      .selection_property = {
        get_selection_property_list(create_property_initializer)
        [RELIABILITY] = {.value = {.simple_preference = NO_PREFERENCE}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTISTREAMING] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ACTIVE_READ_BEFORE_SEND] = {.value = {.simple_preference = PROHIBIT}}, // Temporary - to make it easy to ban quic
      }
    },
    .send = quic_send,
    .init = quic_init,
    .receive = quic_receive,
    .close = quic_close,
    .listen = quic_listen,
    .stop_listen = quic_stop_listen,
    .remote_endpoint_from_peer = quic_remote_endpoint_from_peer
};

#endif //QUIC_H
