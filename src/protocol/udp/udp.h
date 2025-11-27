#ifndef UDP_H
#define UDP_H


#include "ctaps.h"

struct ct_socket_manager_s;

int udp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int udp_close(ct_connection_t* connection);
int udp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int udp_listen(struct ct_socket_manager_s* socket_manager);
int udp_stop_listen(struct ct_socket_manager_s* socket_manager);
int udp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
int udp_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection);
void udp_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection);


const static ct_protocol_impl_t udp_protocol_interface = {
    .name = "UDP",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = REQUIRE}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = PROHIBIT}},
        [ZERO_RTT_MSG] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTISTREAMING] = {.value = {.simple_preference = PROHIBIT}},
        [FULL_CHECKSUM_SEND] = {.value = {.simple_preference = REQUIRE}},
        [FULL_CHECKSUM_RECV] = {.value = {.simple_preference = REQUIRE}},
        [CONGESTION_CONTROL] = {.value = {.simple_preference = PROHIBIT}},
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
    .send = udp_send,
    .init = udp_init,
    .close = udp_close,
    .listen = udp_listen,
    .stop_listen = udp_stop_listen,
    .remote_endpoint_from_peer = udp_remote_endpoint_from_peer,
    .retarget_protocol_connection = udp_retarget_protocol_connection,
    .clone_connection = udp_clone_connection
};

#endif  // UDP_H
