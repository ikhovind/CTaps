#ifndef QUIC_H
#define QUIC_H

#include <connections/connection/connection.h>

#include "protocols/protocol_interface.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct SocketManager;

// Passed as a parameter to picoquic_create()
#define MAX_CONCURRENT_QUIC_CONNECTIONS 256

int quic_init(Connection* connection, const ConnectionCallbacks* connection_callbacks);
int quic_close(const Connection* connection);
int quic_send(Connection* connection, Message* message, MessageContext*);
int quic_listen(struct SocketManager* socket_manager);
int quic_stop_listen(struct SocketManager* listener);
int quic_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer);
void quic_retarget_protocol_connection(Connection* from_connection, Connection* to_connection);

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
    .close = quic_close,
    .listen = quic_listen,
    .stop_listen = quic_stop_listen,
    .remote_endpoint_from_peer = quic_remote_endpoint_from_peer,
    .retarget_protocol_connection = quic_retarget_protocol_connection
};

#endif //QUIC_H
