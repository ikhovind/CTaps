#ifndef TCP_H
#define TCP_H

#include "transport_properties/selection_properties/selection_properties.h"
#include "protocols/protocol_interface.h"
#include <connections/connection/connection.h>

struct ct_socket_manager_t;

int tcp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int tcp_close(const ct_connection_t* connection);
int tcp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int tcp_listen(struct ct_socket_manager_t* socket_manager);
int tcp_stop_listen(struct ct_socket_manager_t* listener);
int tcp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
void tcp_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection);

static ct_protocol_implementation_t tcp_protocol_interface = {
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
    .close = tcp_close,
    .listen = tcp_listen,
    .stop_listen = tcp_stop_listen,
    .remote_endpoint_from_peer = tcp_remote_endpoint_from_peer,
    .retarget_protocol_connection = tcp_retarget_protocol_connection
};

#endif //TCP_H
