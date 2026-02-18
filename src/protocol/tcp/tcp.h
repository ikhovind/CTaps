#ifndef TCP_H
#define TCP_H

#include "ctaps.h"
#include "ctaps_internal.h"

struct ct_socket_manager_s;

// Per-socket TCP state
// Since every TCP connection has its own socket, tcp has no other protocol state
typedef struct ct_tcp_socket_state_s {
  ct_connection_t* connection;
  ct_listener_t* listener;
  ct_message_t* initial_message;
  ct_message_context_t* initial_message_context;
  uv_connect_t* connect_req; // To be freed in tests etc. when we don't run the full connect flow
  uv_tcp_t* tcp_handle;
} ct_tcp_socket_state_t;

int tcp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int tcp_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int tcp_close(ct_connection_t* connection);
int tcp_close_socket(ct_socket_manager_t*);
int tcp_free_socket_state(ct_socket_manager_t* socket_manager);
void tcp_abort(ct_connection_t* connection);
int tcp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t*);
int tcp_listen(struct ct_socket_manager_s* socket_manager);
int tcp_stop_listen(struct ct_socket_manager_s* socket_manager);
int tcp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
int tcp_clone_connection(const struct ct_connection_s* source_connection,
                         struct ct_connection_s* target_connection);
int tcp_free_state(ct_connection_t* connection);
/**
  * @brief No-op, TCP is not multiplexed and therefore has no shared state across cloned connections.
  */
int tcp_free_connection_group_state(ct_connection_group_t* connection_group);

// Protocol interface (definition in tcp.c)
extern const ct_protocol_impl_t tcp_protocol_interface;

#endif //TCP_H
