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

int socket_manager_decrement_ref(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to decrement reference count of NULL socket manager");
    return -EINVAL;
  }
  socket_manager->ref_count--;
  log_debug("Decremented socket manager reference count, updated count: %d", socket_manager->ref_count);
  if (socket_manager->ref_count == 0) {
    int rc = socket_manager->protocol_impl->stop_listen(socket_manager);
    if (rc < 0) {
      log_error("Error stopping socket manager listen: %d", rc);
      return rc;
    }
    free(socket_manager);
  }
  return 0;
}

void socket_manager_increment_ref(ct_socket_manager_t* socket_manager) {
  socket_manager->ref_count++;
  log_debug("Incremented socket manager reference count, updated count: %d", socket_manager->ref_count);
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
  return socket_manager;
}

void ct_socket_manager_free(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to free NULL socket manager");
    return;
  }
  // TODO - free socket manager state
  free(socket_manager);
}

void ct_socket_manager_unref(ct_socket_manager_t* socket_manager) {
  if (!socket_manager) {
    log_warn("Attempted to unreference NULL socket manager");
    return;
  }
  log_debug("Decrementing socket manager reference count with count: %d", socket_manager->ref_count);
  socket_manager->ref_count--;
  if (socket_manager->ref_count == 0) {
    ct_socket_manager_free(socket_manager);
  }
}

int socket_manager_insert_connection(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote, ct_connection_t* connection) {
  log_info("Inserting connection group into socket manager for remote endpoint");
  struct sockaddr_storage remote_addr = remote->data.resolved_address;

  GBytes* addr_bytes = NULL;
  if (remote_addr.ss_family == AF_INET) {
    log_trace("Inserting connection group for IPv4 remote endpoint");
    addr_bytes = g_bytes_new(&remote_addr, sizeof(struct sockaddr_in));
  }
  else if (remote_addr.ss_family == AF_INET6) {
    log_trace("Inserting connection group for IPv6 remote endpoint");
    addr_bytes = g_bytes_new(&remote_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("socket_manager_insert_connection_group encountered unknown address family: %d", remote_addr.ss_family);
    return -EINVAL;
  }
  if (g_hash_table_contains(socket_manager->connections, addr_bytes)) {
    log_error("Connection group for given remote endpoint already exists in socket manager");
    g_bytes_unref(addr_bytes);
    return -EEXIST;
  }

  g_hash_table_insert(socket_manager->connections, addr_bytes, connection);
  connection->socket_manager = ct_socket_manager_ref(socket_manager);
  return 0;
}
