#include "socket_manager.h"

#include "connection/connection.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <endpoint/remote_endpoint.h>
#include <glib.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

ct_connection_t* socket_manager_get_from_demux_table(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr) {
  log_trace("Trying to demux from remote endpoint to connection in socket manager");
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
    log_error("Cannot get connection by unknown address family: %d", remote_addr->ss_family);
    return NULL;
  }
  ct_connection_t* connection =  g_hash_table_lookup(socket_manager->demux_table, addr_bytes);
  g_bytes_unref(addr_bytes);
  if (connection) {
    log_trace("Found connection: %s in socket manager demux table for remote endpoint", connection->uuid);
  }
  else {
    log_trace("No connection found in socket manager demux table for given remote endpoint");
  }
  return connection;
}

ct_socket_manager_t* ct_socket_manager_ref(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to reference NULL socket manager");
    return NULL;
  }
  socket_manager->ref_count++;
  log_debug("Incremented socket manager %p, new reference count is: %d", socket_manager, socket_manager->ref_count);
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
  else {
    log_debug("No protocol-specific socket state to free for protocol: %s in socket manager", socket_manager->protocol_impl->name);
  }
  if (socket_manager->demux_table) {
    g_hash_table_destroy(socket_manager->demux_table);
  }
  g_slist_free(socket_manager->all_connections);
  free(socket_manager);
}

int socket_manager_insert_connection(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote, ct_connection_t* connection) {
  log_trace("Inserting connection: %s into socket manager for remote endpoint", connection->uuid);
  struct sockaddr_storage remote_addr = remote->data.resolved_address;

  if (socket_manager->protocol_impl->protocol_enum == CT_PROTOCOL_UDP) {
    log_trace("Inserting connection into socket manager demux table for UDP protocol");
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
    if (g_hash_table_contains(socket_manager->demux_table, addr_bytes)) {
      log_error("Connection for given remote endpoint already exists in socket manager");
      g_bytes_unref(addr_bytes);
      return -EEXIST;
    }
    g_hash_table_insert(socket_manager->demux_table, addr_bytes, connection);
  }
  socket_manager->all_connections = g_slist_prepend(socket_manager->all_connections, connection);
  connection->socket_manager = ct_socket_manager_ref(socket_manager);
  return 0;
}


int ct_socket_manager_get_num_open_connections(const ct_socket_manager_t* socket_manager) {
  log_trace("Checking how many open connections socket manager has");
  int counter = 0;
  for (GSList* node = socket_manager->all_connections; node != NULL; node = node->next) {
    ct_connection_t* connection = (ct_connection_t*)node->data;
    if (!ct_connection_is_closed(connection)) {
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
  log_debug("Decremented socket manager %p, new reference count is: %d", socket_manager, socket_manager->ref_count);
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
  log_debug("Socket manager closed connection callback invoked for connection: %s", connection->uuid);
  ct_connection_mark_as_closed(connection);

  if (!socket_manager->listener || socket_manager->listener->state == CT_LISTENER_STATE_CLOSED) {
    log_debug("socket manager has closed/no attched listener, checking num open connections");
    int num_open = ct_socket_manager_get_num_open_connections(socket_manager);
    if (num_open == 0) {
      log_debug("Socket manager now has no open connections, closing entire socket manager");
      ct_socket_manager_close(socket_manager);
    }
    else {
      log_debug("Socket manager has %d open connections, not closing socket manager", num_open);
    }
  }
  else {
    log_debug("Socket manager %p has attached listener, not closing socket manager", socket_manager);
  }
  if (connection->connection_callbacks.closed) {
    connection->connection_callbacks.closed(connection);
  }
  else {
    log_debug("Connection has no closed callback registered");
  }
}

void ct_socket_manager_establishment_error_cb(ct_connection_t* connection) {
  ct_socket_manager_t* socket_manager = connection->socket_manager;
  log_debug("Socket manager establishment error callback invoked for connection: %s", connection->uuid);
  ct_connection_mark_as_closed(connection);

  if (!socket_manager->listener || socket_manager->listener->state == CT_LISTENER_STATE_CLOSED) {
    log_debug("socket manager has closed/no attched listener, checking num open connections");
    int num_open = ct_socket_manager_get_num_open_connections(socket_manager);
    if (num_open == 0) {
      log_debug("Socket manager now has no open connections, closing en%tire socket manager");
      ct_socket_manager_close(socket_manager);
    }
    else {
      log_debug("Socket manager has %d open connections, not closing socket manager", num_open);
    }
  }
  else {
    log_debug("Socket manager %p has attached listener, not closing socket manager", socket_manager);
  }
  if (connection->connection_callbacks.establishment_error) {
    connection->connection_callbacks.establishment_error(connection);
  }
}

void ct_socket_manager_aborted_connection_cb(ct_connection_t* connection) {
  ct_socket_manager_t* socket_manager = connection->socket_manager;
  log_debug("Socket manager aborted connection callback invoked for connection: %s", connection->uuid);
  ct_connection_mark_as_closed(connection);

  if (!socket_manager->listener || socket_manager->listener->state == CT_LISTENER_STATE_CLOSED) {
    log_debug("socket manager has closed/no attched listener, checking num open connections");
    int num_open = ct_socket_manager_get_num_open_connections(socket_manager);
    if (num_open == 0) {
      log_debug("Socket manager now has no open connections, closing entire socket manager");
      ct_socket_manager_close(socket_manager);
    }
    else {
      log_debug("Socket manager has %d open connections, not closing socket manager", num_open);
    }
  }
  else {
    log_debug("Socket manager %p has attached listener, not closing socket manager", socket_manager);
  }
  if (connection->connection_callbacks.connection_error) {
    connection->connection_callbacks.connection_error(connection);
  }
  else {
    log_debug("No connection error callback registered for connection: %s", connection->uuid);
  }
}

int ct_socket_manager_close_connection(ct_socket_manager_t* socket_manager, ct_connection_t* connection) {
  if (!socket_manager || !connection) {
    log_error("NULL parameter passed to socket manager close connection");
    log_debug("socket mangager: %p, connection: %p", socket_manager, connection); 
  }
  log_debug("Socket manager: Closing attached connection: %s", connection->uuid);
  int rc = socket_manager->protocol_impl->close(connection);
  if (rc) {
    log_error("Error from protocol when closing connection: %s", connection->uuid);
    return rc;
  }
  return 0;
}

int ct_socket_manager_listener_stop(ct_socket_manager_t* socket_manager) {
  log_debug("Socket manager: closing attached listener");
  ct_listener_t* listener = socket_manager->listener;
  listener->state = CT_LISTENER_STATE_CLOSED;

  socket_manager->protocol_impl->stop_listen(socket_manager);

  int num_open = ct_socket_manager_get_num_open_connections(socket_manager);
  if (num_open == 0) {
    log_debug("Socket manager now has no open connections, closing entire socket manager");
    ct_socket_manager_close(socket_manager);
  }
  else {
    log_debug("Socket manager has %d open connections after stopping listener, not closing socket manager", num_open);
  }

  if (listener->listener_callbacks.stopped) {
    log_debug("Invoking listener stopped callback");
    listener->listener_callbacks.stopped(listener);
  }
  else {
    log_debug("No listener stopped callback registered");
  }
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

  if (protocol_impl->protocol_enum == CT_PROTOCOL_UDP) {
    socket_manager->demux_table = g_hash_table_new(g_bytes_hash, g_bytes_equal);
  }
  socket_manager->all_connections = NULL;
  socket_manager->callbacks.closed_connection = ct_socket_manager_closed_connection_cb;
  socket_manager->callbacks.aborted_connection = ct_socket_manager_aborted_connection_cb;
  socket_manager->callbacks.establishment_error = ct_socket_manager_establishment_error_cb;
  log_debug("Created new socket manager: %p for protocol: %s", socket_manager, protocol_impl->name);
  return socket_manager;
}

