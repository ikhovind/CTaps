#ifndef TCP_H
#define TCP_H

#include "ctaps.h"
#include "ctaps_internal.h"

struct ct_socket_manager_s;

int tcp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);
int tcp_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int tcp_close(ct_connection_t* connection, void(*on_connection_close)(ct_connection_t* connection));
int tcp_close_socket(ct_socket_manager_t*);
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
