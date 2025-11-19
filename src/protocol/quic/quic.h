#ifndef QUIC_H
#define QUIC_H


#include "ctaps.h"

struct ct_socket_manager_t;

// Passed as a parameter to picoquic_create()
#define MAX_CONCURRENT_QUIC_CONNECTIONS 256

int quic_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int quic_close(const ct_connection_t* connection);
int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int quic_listen(struct ct_socket_manager_t* socket_manager);
int quic_stop_listen(struct ct_socket_manager_t* listener);
int quic_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
void quic_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection);

static ct_protocol_implementation_t quic_protocol_interface = {
    .name = "QUIC",
    .selection_properties = {
      .selection_property = {
        get_selection_property_list(create_sel_property_initializer)
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
