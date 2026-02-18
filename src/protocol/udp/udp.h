#ifndef UDP_H
#define UDP_H


#include "ctaps.h"
#include "ctaps_internal.h"

typedef struct ct_udp_socket_state_s {
  uv_udp_t* udp_handle;
} ct_udp_socket_state_t;

ct_udp_socket_state_t* ct_udp_socket_state_new(uv_udp_t* udp_handle);
int udp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int udp_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int udp_close(ct_connection_t* connection);
int udp_close_socket(ct_socket_manager_t*);
void udp_abort(ct_connection_t* connection);
int udp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int udp_listen(struct ct_socket_manager_s* socket_manager);
int udp_stop_listen(struct ct_socket_manager_s* socket_manager);
int udp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
int udp_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection);
int udp_free_state(ct_connection_t* connection);
int udp_free_socket_state(ct_socket_manager_t* socket_manager);
/**
  * @brief No-op, UDP is not multiplexed and therefore has no shared state across cloned connections.
  */
int udp_free_connection_group_state(ct_connection_group_t* connection_group);

// Protocol interface (definition in udp.c)
extern const ct_protocol_impl_t udp_protocol_interface;

#endif  // UDP_H
