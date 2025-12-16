#ifndef TCP_H
#define TCP_H

#include "ctaps.h"

struct ct_socket_manager_s;

int tcp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int tcp_close(ct_connection_t* connection);
int tcp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int tcp_listen(struct ct_socket_manager_s* socket_manager);
int tcp_stop_listen(struct ct_socket_manager_s* socket_manager);
int tcp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
void tcp_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection);
int tcp_clone_connection(const struct ct_connection_s* source_connection,
                         struct ct_connection_s* target_connection);

static const ct_protocol_impl_t tcp_protocol_interface = {
    .name = "TCP",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = PROHIBIT}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = REQUIRE}},
        [ZERO_RTT_MSG] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTISTREAMING] = {.value = {.simple_preference = PROHIBIT}},
        [FULL_CHECKSUM_SEND] = {.value = {.simple_preference = REQUIRE}},
        [FULL_CHECKSUM_RECV] = {.value = {.simple_preference = REQUIRE}},
        [CONGESTION_CONTROL] = {.value = {.simple_preference = REQUIRE}},
        [KEEP_ALIVE] = {.value = {.simple_preference = NO_PREFERENCE}},
        [INTERFACE] = {.value = {.simple_preference = NO_PREFERENCE}},
        [PVD] = {.value = {.simple_preference = NO_PREFERENCE}},
        [USE_TEMPORARY_LOCAL_ADDRESS] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTIPATH] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ADVERTISES_ALT_ADDRES] = {.value = {.simple_preference = NO_PREFERENCE}},
        [DIRECTION] = {.value = {.simple_preference = NO_PREFERENCE}},
        [SOFT_ERROR_NOTIFY] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ACTIVE_READ_BEFORE_SEND] = {.value = {.simple_preference = NO_PREFERENCE}},
      }
    },
    .send = tcp_send,
    .init = tcp_init,
    .close = tcp_close,
    .listen = tcp_listen,
    .stop_listen = tcp_stop_listen,
    .remote_endpoint_from_peer = tcp_remote_endpoint_from_peer,
    .retarget_protocol_connection = tcp_retarget_protocol_connection,
    .clone_connection = tcp_clone_connection
};

#endif //TCP_H
