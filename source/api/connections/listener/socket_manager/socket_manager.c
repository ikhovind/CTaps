#include "socket_manager.h"

#include <ctaps.h>
#include <connections/connection/connection.h>
#include <connections/listener/listener.h>
#include <glib/gbytes.h>

void socket_manager_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  // We'll use a static buffer for this simple example, but in a real
  // application, you would likely use malloc or a buffer pool.
  static char slab[65536];
  *buf = uv_buf_init(slab, sizeof(slab));
}

int socket_manager_create(SocketManager* socket_manager, Listener* listener) {
  printf("Creating socket manager\n");
  socket_manager->listener = listener;
  printf("Listener local endpoint type is 2 %d\n", listener->local_endpoint.type);
  printf("Socket manager listener type is 3 %d\n", socket_manager->listener->local_endpoint.type);

  socket_manager->protocol_impl.listen(socket_manager);

  return 0;
}

int socket_manager_remove_connection(SocketManager* socket_manager, Connection* connection) {
  printf("Removing connection from socket manager\n");
  GBytes* addr_bytes = g_bytes_new(&connection->remote_endpoint.data.address, sizeof(struct sockaddr_in));
  gboolean removed = g_hash_table_remove(socket_manager->active_connections, addr_bytes);
  if (removed) {
    socket_manager->ref_count--;
    printf("Connection removed successfully, new ref count: %d\n", socket_manager->ref_count);
    if (socket_manager->ref_count == 0) {
      socket_manager->protocol_impl.stop_listen(socket_manager);
      free(socket_manager);
    }
    return 0;
  }
  printf("Connection not found in hash table\n");
  return -1;
}

void socket_manager_multiplex_received_message(SocketManager* socket_manager, Message* message, const struct sockaddr* addr) {
  printf("Socket manager read callback with message: %s\n", message->content);

  Listener* listener = socket_manager->listener;

  // get source address
  printf("Bytes pointer is: %p\n", addr);
  GBytes* addr_bytes = g_bytes_new(addr, sizeof(struct sockaddr_in));
  Connection* connection = g_hash_table_lookup(socket_manager->active_connections, addr_bytes);

  if (connection == NULL && socket_manager->listener != NULL) {
    printf("No connection found, creating new one\n");

    RemoteEndpoint remote_endpoint;
    remote_endpoint_from_sockaddr(&remote_endpoint, addr);

    connection = malloc(sizeof(Connection));
    connection_build_from_listener(connection, listener, &remote_endpoint);
    // insert connection into hash table
    g_hash_table_insert(socket_manager->active_connections, addr_bytes, connection);
    socket_manager->ref_count++;

    g_queue_push_tail(connection->received_messages, message);
    listener->connection_received_cb(listener, connection);
  }
  else if (connection != NULL) {
    printf("Connection found, using existing one\n");
    if (g_queue_is_empty(connection->received_callbacks)) {
      g_queue_push_tail(connection->received_messages, message);
    }
    else {
      printf("We have a receive callback ready\n");
      ReceiveMessageRequest* receive_callback = g_queue_pop_head(connection->received_callbacks);

      receive_callback->receive_cb(connection, &message, receive_callback->user_data);
      free(receive_callback);
    }
  } else {
    printf("Received message from new connection on closed listener, ignoring\n");
  }
}
