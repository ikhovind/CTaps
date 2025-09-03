#ifndef SOCKET_MANAGER_H
#define SOCKET_MANAGER_H
#include <uv.h>
#include <endpoints/local/local_endpoint.h>
#include <glib.h>
#include <connections/connection/connection.h>

struct Listener;

typedef struct SocketManager{
  uv_udp_t udp_handle;
  int ref_count; // Number of objects using this socket (Listener + Connections)
  GHashTable* active_connections;
  uv_udp_recv_cb on_read;
  ProtocolImplementation protocol_impl;
  struct Listener* listener;
} SocketManager;

int socket_manager_remove_connection(SocketManager* socket_manager, Connection* connection);

int socket_manager_create(SocketManager* socket_manager, struct Listener* listener);

void socket_manager_read(SocketManager* socket_manager, Message* message, const struct sockaddr* addr);

void socket_manager_free(SocketManager* manager);

#endif //SOCKET_MANAGER_H
