#include "socket_manager.h"

#include <connections/connection/connection.h>
#include <connections/listener/listener.h>
#include <glib.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>

#include "protocols/protocol_interface.h"
#include "ctaps.h"

void socket_manager_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  // We'll use a static buffer for this simple example, but in a real
  // application, you would likely use malloc or a buffer pool.
  static char slab[65536];
  *buf = uv_buf_init(slab, sizeof(slab));
}

int socket_manager_build(SocketManager* socket_manager, Listener* listener) {
  log_debug("Building socket manager for listener");
  socket_manager->listener = listener;

  return socket_manager->protocol_impl.listen(socket_manager);
}

int socket_manager_remove_connection(SocketManager* socket_manager, const Connection* connection) {
  log_debug("Removing connection from socket manager: %p", (void*)connection);
  GBytes* addr_bytes = NULL;
  if (connection->remote_endpoint.data.resolved_address.ss_family == AF_INET) {
    log_trace("Removing IPv4 connection from socket manager");
    addr_bytes = g_bytes_new(&connection->remote_endpoint.data.resolved_address, sizeof(struct sockaddr_in));
  }
  else if (connection->remote_endpoint.data.resolved_address.ss_family == AF_INET6) {
    log_trace("Removing IPv6 connection from socket manager");
    addr_bytes = g_bytes_new(&connection->remote_endpoint.data.resolved_address, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("socket_manager_remove_connection encountered connection with unknown address family: %d", connection->remote_endpoint.data.resolved_address.ss_family);
    return -EINVAL;
  }
  log_trace("Hash code of addr_bytes when removing is: %u", g_bytes_hash(addr_bytes));
  // print gbytes:
  const gboolean removed = g_hash_table_remove(socket_manager->active_connections, addr_bytes);
  if (removed) {
    // Log before since decrementing might free the socket manager
    log_info("Connection removed successfully, new socket manager ref count: %d", socket_manager->ref_count);
    socket_manager_decrement_ref(socket_manager);
    return 0;
  }
  log_warn("Could not remove Connection from socket manager hash table");
  return -EINVAL;
}

void socket_manager_decrement_ref(SocketManager* socket_manager) {
  socket_manager->ref_count--;
  log_debug("Decremented socket manager reference count, updated count: %d", socket_manager->ref_count);
  if (socket_manager->ref_count == 0) {
    int rc = socket_manager->protocol_impl.stop_listen(socket_manager);
    if (rc < 0) {
      log_error("Error stopping socket manager listen: %d", rc);
    }
    free(socket_manager);
  }
}

void socket_manager_increment_ref(SocketManager* socket_manager) {
  socket_manager->ref_count++;
  log_debug("Incremented socket manager reference count, updated count: %d", socket_manager->ref_count);
}

Connection* socket_manager_get_connection_from_remote(SocketManager* socket_manager, const struct sockaddr_storage* remote_addr, bool* was_new) {
  *was_new = false;
  GBytes* addr_bytes = NULL;
  if (remote_addr->ss_family == AF_INET) {
    log_debug("socket_manager_get_connection_from_remote received IPv4 address");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in));
  }
  else if (remote_addr->ss_family == AF_INET6) {
    log_debug("socket_manager_get_connection_from_remote received IPv6 address");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("socket_manager_get_connection_from_remote encountered unknown address family: %d", remote_addr->ss_family);
    return NULL;
  }
  Connection* connection = g_hash_table_lookup(socket_manager->active_connections, addr_bytes);
  Listener* listener = socket_manager->listener;
  
  // This means we have received a message from a new remote endpoint
  if (connection == NULL) {
    log_debug("Socket manager did not find existing connection for remote endpoint");
    if (socket_manager->listener == NULL) {
      log_debug("Socket manager is not accepting new connections, ignoring");
      return NULL;
    }
    log_debug("No connection found for remote endpoint in socket manager, creating new one");

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_from_sockaddr(&remote_endpoint, (struct sockaddr_storage*)remote_addr);

    connection = malloc(sizeof(Connection));
    if (connection == NULL) {
      log_error("Failed to allocate memory for new connection");
      g_bytes_unref(addr_bytes);
      return NULL;
    }
    connection_build_multiplexed(connection, listener, &remote_endpoint);
    log_trace("Hash code of addr_bytes when inserting is: %u", g_bytes_hash(addr_bytes));
    g_hash_table_insert(socket_manager->active_connections, addr_bytes, connection);
    log_debug("Inserted new connection into socket manager hash table");
    socket_manager->ref_count++;
    log_debug("Socket manager reference count is now: %d", socket_manager->ref_count);
    *was_new = true;
  }
  else {
    log_debug("Found existing connection for remote endpoint in socket manager");
  }
  return connection;
}

void socket_manager_multiplex_received_message(SocketManager* socket_manager, Message* message, const struct sockaddr_storage* addr) {
  log_trace("Socket manager received message, multiplexing to connection");

  bool was_new = false;
  Connection* connection = socket_manager_get_connection_from_remote(socket_manager, addr, &was_new);
  Listener* listener = socket_manager->listener;

  if (connection != NULL) {
    if (was_new) {
      log_debug("Socket manager invoking listener callback for new connection");
      listener->listener_callbacks.connection_received(listener, connection, listener->listener_callbacks.user_data);
    }
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
  }
}
