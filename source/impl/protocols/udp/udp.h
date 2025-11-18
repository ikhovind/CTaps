#ifndef UDP_H
#define UDP_H


#include "ctaps.h"

struct ct_socket_manager_t;

int udp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int udp_close(const ct_connection_t* connection);
int udp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int udp_listen(struct ct_socket_manager_t* socket_manager);
int udp_stop_listen(struct ct_socket_manager_t* socket_manager);
int udp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
void udp_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection);


static ct_protocol_implementation_t udp_protocol_interface = {
    .name = "UDP",
    .selection_properties = {
      .selection_property = {
        get_selection_property_list(create_sel_property_initializer)
        [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = PROHIBIT}},
      }
    },
    .send = udp_send,
    .init = udp_init,
    .close = udp_close,
    .listen = udp_listen,
    .stop_listen = udp_stop_listen,
    .remote_endpoint_from_peer = udp_remote_endpoint_from_peer,
    .retarget_protocol_connection = udp_retarget_protocol_connection
};

#endif  // UDP_H
