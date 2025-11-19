#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H
#include <uv.h>
#include "ctaps.h"
#include <glib.h>

struct ct_listener_t;

/*
 * TODO - this is fairly UDP-specific as for now
 * - Multiplexing messages is really only relevant for connectionless protocols
 * - For TCP, the socket manager would instead have to manage accepting messages on behalf of a ct_connection_t
 *
 * This means that the protocol_uv_handle is only used for UDP right now
 */
typedef struct ct_socket_manager_t {
  void* protocol_state;
  int ref_count; // Number of objects using this socket (ct_listener_t + Connections)
  GHashTable* active_connections;
  uv_udp_recv_cb on_read;
  ct_protocol_implementation_t protocol_impl;
  struct ct_listener_t* listener;
} ct_socket_manager_t;

int socket_manager_remove_connection(ct_socket_manager_t* socket_manager, const ct_connection_t* connection);

int socket_manager_build(ct_socket_manager_t* socket_manager, struct ct_listener_t* listener);

void socket_manager_multiplex_received_message(ct_socket_manager_t* socket_manager, ct_message_t* message, const struct sockaddr_storage* addr);

void socket_manager_decrement_ref(ct_socket_manager_t* socket_manager);

void socket_manager_increment_ref(ct_socket_manager_t* socket_manager);

void socket_manager_free(ct_socket_manager_t* socket_manager);

void new_stream_connection_cb(uv_stream_t *server, int status);

ct_connection_t* socket_manager_get_or_create_connection(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr, bool* was_new);

#endif //SOCKET_MANAGER_H
