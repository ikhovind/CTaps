#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H
#include <uv.h>
#include <endpoints/local/local_endpoint.h>
#include <glib.h>
#include <connections/connection/connection.h>

struct Listener;

/*
 * TODO - this is fairly UDP-specific as for now
 * - Multiplexing messages is really only relevant for connectionless protocols
 * - For TCP, the socket manager would instead have to manage accepting messages on behalf of a Connection
 *
 * This means that the protocol_uv_handle is only used for UDP right now
 */
typedef struct SocketManager{
  void* protocol_state;
  int ref_count; // Number of objects using this socket (Listener + Connections)
  GHashTable* active_connections;
  uv_udp_recv_cb on_read;
  ProtocolImplementation protocol_impl;
  struct Listener* listener;
} SocketManager;

int socket_manager_remove_connection(SocketManager* socket_manager, const Connection* connection);

int socket_manager_build(SocketManager* socket_manager, struct Listener* listener);

void socket_manager_multiplex_received_message(SocketManager* socket_manager, Message* message, const struct sockaddr_storage* addr);

void socket_manager_decrement_ref(SocketManager* socket_manager);

void socket_manager_increment_ref(SocketManager* socket_manager);

void socket_manager_free(SocketManager* socket_manager);

void new_stream_connection_cb(uv_stream_t *server, int status);

Connection* socket_manager_get_or_create_connection(SocketManager* socket_manager, const struct sockaddr_storage* remote_addr, bool* was_new);

#endif //SOCKET_MANAGER_H
