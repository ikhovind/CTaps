#ifndef UDP_H
#define UDP_H


#include "ctaps.h"
#include "ctaps_internal.h"

typedef struct ct_udp_socket_state_s {
  uv_udp_t* udp_handle;
} ct_udp_socket_state_t;

typedef struct udp_send_data_s {
  ct_connection_t* connection;
  ct_message_t* message;
  ct_message_context_t* message_context;
} udp_send_data_t;

ct_udp_socket_state_t* ct_udp_socket_state_new(uv_udp_t* udp_handle);
int udp_init(ct_connection_t* connection);
int udp_init_with_send(ct_connection_t* connection, ct_message_t* initial_message, ct_message_context_t* initial_message_context);
int udp_close(ct_connection_t* connection);
void udp_close_socket(ct_socket_manager_t*);
void udp_abort(ct_connection_t* connection);
int udp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context);
int udp_listen(struct ct_socket_manager_s* socket_manager);
void udp_close_listener(struct ct_socket_manager_s* socket_manager);
int udp_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection);
void udp_free_state(ct_connection_t* connection);
void udp_free_socket_state(ct_socket_manager_t* socket_manager);
/**
  * @brief No-op, UDP is not multiplexed and therefore has no shared state across cloned connections.
  */
void udp_free_connection_group_state(ct_connection_group_t* connection_group);
void udp_close_connection_group(ct_connection_group_t* connection_group);

// Protocol interface (definition in udp.c)
extern const ct_protocol_impl_t udp_protocol_interface;

#endif  // UDP_H
