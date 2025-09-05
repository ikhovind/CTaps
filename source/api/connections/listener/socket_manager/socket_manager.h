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
  uv_handle_t* protocol_uv_handle;
  int ref_count; // Number of objects using this socket (Listener + Connections)
  GHashTable* active_connections;
  uv_udp_recv_cb on_read;
  ProtocolImplementation protocol_impl;
  struct Listener* listener;
} SocketManager;

int socket_manager_remove_connection(SocketManager* socket_manager, Connection* connection);

int socket_manager_create(SocketManager* socket_manager, struct Listener* listener);

void socket_manager_multiplex_received_message(SocketManager* socket_manager, Message* message, const struct sockaddr* addr);

void socket_manager_free(SocketManager* manager);

#endif //SOCKET_MANAGER_H
