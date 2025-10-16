#include "socket_manager.h"

#include <connections/connection/connection.h>
#include <connections/listener/listener.h>
#include <glib.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include "protocols/protocol_interface.h"

void socket_manager_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  // We'll use a static buffer for this simple example, but in a real
  // application, you would likely use malloc or a buffer pool.
  static char slab[65536];
  *buf = uv_buf_init(slab, sizeof(slab));
}

int socket_manager_build(SocketManager* socket_manager, Listener* listener) {
  socket_manager->listener = listener;

  return socket_manager->protocol_impl.listen(socket_manager);
}

int socket_manager_remove_connection(SocketManager* socket_manager, const Connection* connection) {
  log_debug("Removing connection from socket manager");
  const GBytes* addr_bytes = g_bytes_new(&connection->remote_endpoint.data.resolved_address, sizeof(struct sockaddr_in));
  const gboolean removed = g_hash_table_remove(socket_manager->active_connections, addr_bytes);
  if (removed) {
    log_info("Connection removed successfully, new socket manager ref count: %d", socket_manager->ref_count - 1);
    socket_manager_decrement_ref(socket_manager);
    return 0;
  }
  log_warn("Could not remove Connection from socket manager hash table");
  return -EINVAL;
}

void socket_manager_decrement_ref(SocketManager* socket_manager) {
  socket_manager->ref_count--;
  if (socket_manager->ref_count == 0) {
    socket_manager->protocol_impl.stop_listen(socket_manager);
    free(socket_manager);
    socket_manager = NULL;
  }
}

void socket_manager_multiplex_received_message(SocketManager* socket_manager, Message* message, const struct sockaddr* addr) {
  log_trace("Socket manager received message, multiplexing to connection");

  Listener* listener = socket_manager->listener;

  // get source resolved_address
  GBytes* addr_bytes = g_bytes_new(addr, sizeof(struct sockaddr_in));
  Connection* connection = g_hash_table_lookup(socket_manager->active_connections, addr_bytes);

  if (connection == NULL && socket_manager->listener != NULL) {
    log_debug("No connection found, creating new one\n");

    RemoteEndpoint remote_endpoint;
    remote_endpoint_from_sockaddr(&remote_endpoint, addr);

    connection = malloc(sizeof(Connection));
    if (connection == NULL) {
      log_error("Failed to allocate memory for new connection\n");
      g_bytes_unref(addr_bytes);
      return;
    }
    connection_build_from_listener(connection, listener, &remote_endpoint);
    // insert connection into hash table
    g_hash_table_insert(socket_manager->active_connections, addr_bytes, connection);
    socket_manager->ref_count++;

    g_queue_push_tail(connection->received_messages, message);
    listener->listener_callbacks.connection_received(listener, connection, listener->listener_callbacks.user_data);
  }
  else if (connection != NULL) {
    log_debug("Connection found, using existing one\n");
    if (g_queue_is_empty(connection->received_callbacks)) {
      log_debug("Found Connection has no receive callback ready, queueing message");
      g_queue_push_tail(connection->received_messages, message);
    }
    else {
      log_debug("Found Connection has receive callback ready, invoking it");
      ReceiveCallbacks* receive_callback = g_queue_pop_head(connection->received_callbacks);

      receive_callback->receive_callback(connection, &message, NULL, receive_callback->user_data);
      free(receive_callback);
    }
  } else {
    log_debug("Received new connection on closed listener, ignoring");
  }
}
