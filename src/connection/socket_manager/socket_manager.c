#include "socket_manager.h"

#include "connection/connection.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <endpoint/remote_endpoint.h>
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

int socket_manager_build(ct_socket_manager_t* socket_manager, ct_listener_t* listener) {
  log_debug("Building socket manager for listener");
  // Hash connections by remote endpoint because incoming packets only provide
  // the remote and local address - so in cases where we share a local address
  // for multiple connections we must we demultiplex to the correct connection
  socket_manager->connections = g_hash_table_new(g_bytes_hash, g_bytes_equal);
  socket_manager->listener = listener;
  return 0;
}

ct_socket_manager_t* ct_socket_manager_new(const ct_protocol_impl_t* protocol_impl, ct_listener_t* listener) {
  ct_socket_manager_t* socket_manager = malloc(sizeof(ct_socket_manager_t));
  if (!socket_manager) {
    log_error("Failed to allocate memory for socket manager");
    return NULL;
  }
  memset(socket_manager, 0, sizeof(ct_socket_manager_t));
  socket_manager->protocol_impl = protocol_impl;
  socket_manager->listener = listener;
  socket_manager->connections = g_hash_table_new(g_bytes_hash, g_bytes_equal);
  return socket_manager;
}

ct_connection_t* socket_manager_get_connection(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr) {
  log_info("Trying to get connection group for remote endpoint in socket manager");
  GBytes* addr_bytes = NULL;
  if (remote_addr->ss_family == AF_INET) {
    log_trace("Getting connection group by IPv4 address");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in));
  }
  else if (remote_addr->ss_family == AF_INET6) {
    log_trace("Getting connection group by IPv6 address");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("Cannot get connection group by unknown address family: %d", remote_addr->ss_family);
    return NULL;
  }

  ct_connection_t* connection =  g_hash_table_lookup(socket_manager->connections, addr_bytes);
  g_bytes_unref(addr_bytes);
  return connection;
}

ct_socket_manager_t* ct_socket_manager_ref(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to reference NULL socket manager");
    return NULL;
  }
  socket_manager->ref_count++;
  log_trace("Referenced socket manager, new count is: %d", socket_manager->ref_count);
  return socket_manager;
}

void ct_socket_manager_free(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to free NULL socket manager");
    return;
  }

  if (socket_manager->protocol_impl->free_socket_state) {
    socket_manager->protocol_impl->free_socket_state(socket_manager);
  }
  g_hash_table_destroy(socket_manager->connections);
  free(socket_manager);
}

int socket_manager_insert_connection(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote, ct_connection_t* connection) {
  log_info("Inserting connection into socket manager for remote endpoint");
  struct sockaddr_storage remote_addr = remote->data.resolved_address;

  GBytes* addr_bytes = NULL;
  if (remote_addr.ss_family == AF_INET) {
    log_trace("Inserting connection for IPv4 remote endpoint");
    addr_bytes = g_bytes_new(&remote_addr, sizeof(struct sockaddr_in));
  }
  else if (remote_addr.ss_family == AF_INET6) {
    log_trace("Inserting connection for IPv6 remote endpoint");
    addr_bytes = g_bytes_new(&remote_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("socket_manager_insert_connection encountered unknown address family: %d", remote_addr.ss_family);
    return -EINVAL;
  }
  if (g_hash_table_contains(socket_manager->connections, addr_bytes)) {
    log_error("Connection for given remote endpoint already exists in socket manager");
    g_bytes_unref(addr_bytes);
    return -EEXIST;
  }

  g_hash_table_insert(socket_manager->connections, addr_bytes, connection);
  connection->socket_manager = ct_socket_manager_ref(socket_manager);
  return 0;
}


int ct_socket_manager_get_num_open_connections(const ct_socket_manager_t* socket_manager) {
  int counter = 0;
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, socket_manager->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* connection = (ct_connection_t*)value;
    if (!ct_connection_is_closed(connection)) {
      log_debug("Not counting connection: %s as closed, state is: %d", connection->uuid, connection->transport_properties->connection_properties.list[STATE].value.enum_val);
      counter++;
    }
  }
  return counter;
}

void ct_socket_manager_unref(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to unreference NULL socket manager");
    return;
  }
  socket_manager->ref_count--;
  log_debug("Decremented socket manager reference count, new count is: %d", socket_manager->ref_count);
  if (socket_manager->ref_count == 0) {
    log_trace("Socket manager reference count is zero, freeing socket manager");
    ct_socket_manager_free(socket_manager);
  }
}


void ct_socket_manager_handle_closed_connection(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("NULL socket manager parameter for ct_socket_manager_handle_closed_connection");
    return;
  }
  if (!socket_manager->listener) {
    if (ct_socket_manager_get_num_open_connections(socket_manager) == 0) {
      ct_socket_manager_close(socket_manager);
    }
  }
}

void ct_socket_manager_close(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("NULL socket manager parameter for socket manager close");
    return;
  }
  socket_manager->protocol_impl->close_socket(socket_manager);
}

void ct_socket_manager_closed_connection_cb(ct_connection_t* connection) {
  ct_socket_manager_t* socket_manager = connection->socket_manager;
  ct_connection_mark_as_closed(connection);

  if (!socket_manager->listener) {
    log_debug("socket manager has no attched listener, checking num open connections");
    int num_open = ct_socket_manager_get_num_open_connections(socket_manager);
    // TODO this fails since connection is "closing" not "closed"
    if (num_open == 0) {
      log_debug("Socket manager now has no open connections, closing entire socket manager");
      ct_socket_manager_close(socket_manager);
    }
    else {
      log_debug("Socket manager has %d open connections, not closing socket manager", num_open);
    }
  }
  if (connection->connection_callbacks.closed) {
    connection->connection_callbacks.closed(connection);
  }
}

int ct_socket_manager_close_connection(ct_socket_manager_t* socket_manager, ct_connection_t* connection) {
  log_debug("Socket manager: Closing attached connection");
  if (!socket_manager || !connection) {
    log_error("NULL parameter passed to socket manager close connection");
    log_debug("socket mangager: %p, connection: %p", socket_manager, connection); 
  }
  int rc = socket_manager->protocol_impl->close(connection, ct_socket_manager_closed_connection_cb);
  if (rc) {
    log_error("Error from protocol when closing connection: %s", connection->uuid);
    return rc;
  }
  return 0;
}
