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
  // Hash connection groups by remote endpoint because incoming packets only provide
  // the remote address - we demultiplex to the correct connection group
  //
  // This connection group is associated with a single socket. Any further
  // demultiplexing to individual connections within the group is protocol-specific.
  socket_manager->connection_groups = g_hash_table_new(g_bytes_hash, g_bytes_equal);
  socket_manager->listener = listener;
  return 0;
}

int socket_manager_remove_connection_group(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr) {
  log_debug("Removing connection group from socket manager for remote endpoint");

  GBytes* addr_bytes = NULL;
  if (remote_addr->ss_family == AF_INET) {
    log_trace("Removing connection group for IPv4 remote endpoint");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in));
  }
  else if (remote_addr->ss_family == AF_INET6) {
    log_trace("Removing connection group for IPv6 remote endpoint");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("socket_manager_remove_connection_group encountered unknown address family: %d", remote_addr->ss_family);
    return -EINVAL;
  }

  log_trace("Hash code of addr_bytes when removing is: %u", g_bytes_hash(addr_bytes));
  const gboolean removed = g_hash_table_remove(socket_manager->connection_groups, addr_bytes);
  g_bytes_unref(addr_bytes);

  if (removed) {
    // Log before since decrementing might free the socket manager
    log_info("Connection group removed successfully, decrementing socket manager reference count");
    socket_manager_decrement_ref(socket_manager);
    return 0;
  }
  log_warn("Could not remove connection group from socket manager hash table");
  return -EINVAL;
}

void socket_manager_decrement_ref(ct_socket_manager_t* socket_manager) {
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

void socket_manager_increment_ref(ct_socket_manager_t* socket_manager) {
  socket_manager->ref_count++;
  log_debug("Incremented socket manager reference count, updated count: %d", socket_manager->ref_count);
}

ct_connection_group_t* socket_manager_get_or_create_connection_group(ct_socket_manager_t* socket_manager, const struct sockaddr_storage* remote_addr, bool* was_new) {
  log_info("Trying to fetch or create connection group for remote endpoint in socket manager");
  if (was_new) {
    *was_new = false;
  }
  GBytes* addr_bytes = NULL;
  if (remote_addr->ss_family == AF_INET) {
    log_debug("socket_manager_get_or_create_connection_group received IPv4 address");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in));
  }
  else if (remote_addr->ss_family == AF_INET6) {
    log_debug("socket_manager_get_or_create_connection_group received IPv6 address");
    addr_bytes = g_bytes_new(remote_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("socket_manager_get_or_create_connection_group encountered unknown address family: %d", remote_addr->ss_family);
    return NULL;
  }

  // Look up connection group by remote endpoint
  ct_connection_group_t* connection_group = g_hash_table_lookup(socket_manager->connection_groups, addr_bytes);
  ct_listener_t* listener = socket_manager->listener;

  // This means we have received a message from a new remote endpoint
  if (connection_group == NULL) {
    log_debug("Socket manager did not find existing connection group for remote endpoint");
    if (socket_manager->listener == NULL) {
      log_debug("Socket manager is not accepting new connections, ignoring");
      g_bytes_unref(addr_bytes);
      return NULL;
    }
    log_debug("No connection group found for remote endpoint, creating new one with first connection");

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_from_sockaddr(&remote_endpoint, (struct sockaddr_storage*)remote_addr);

    // Create first connection for this remote endpoint
    ct_connection_t* connection = create_empty_connection_with_uuid();
    if (connection == NULL) {
      log_error("Failed to allocate memory for new connection");
      g_bytes_unref(addr_bytes);
      return NULL;
    }
    int rc = ct_connection_build_multiplexed(connection, listener, &remote_endpoint);
    if (rc < 0) {
      log_error("Failed to build multiplexed connection for new connection group: %d", rc);
      ct_connection_free(connection);
      g_bytes_unref(addr_bytes);
      return NULL;
    }

    connection_group = connection->connection_group;

    log_trace("Hash code of addr_bytes when inserting is: %u", g_bytes_hash(addr_bytes));
    g_hash_table_insert(socket_manager->connection_groups, addr_bytes, connection_group);
    log_debug("Inserted new connection group into socket manager hash table");
    socket_manager->ref_count++;
    log_debug("Socket manager reference count is now: %d", socket_manager->ref_count);
    if (was_new) {
      *was_new = true;
    }
  }
  else {
    log_debug("Found existing connection group for remote endpoint in socket manager");
    g_bytes_unref(addr_bytes);
  }
  return connection_group;
}

