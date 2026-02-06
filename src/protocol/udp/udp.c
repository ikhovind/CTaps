#include "udp.h"

#include "connection/connection.h"
#include "connection/connection_group.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <protocol/common/socket_utils.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <uv.h>

#define MAX_FOUND_INTERFACE_ADDRS 64

// Protocol interface definition (moved from header to access internal struct)
const ct_protocol_impl_t udp_protocol_interface = {
    .name = "UDP",
    .protocol_enum = CT_PROTOCOL_UDP,
    .supports_alpn = false,
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = REQUIRE}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = PROHIBIT}},
        [ZERO_RTT_MSG] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTISTREAMING] = {.value = {.simple_preference = PROHIBIT}},
        [FULL_CHECKSUM_SEND] = {.value = {.simple_preference = REQUIRE}},
        [FULL_CHECKSUM_RECV] = {.value = {.simple_preference = REQUIRE}},
        [CONGESTION_CONTROL] = {.value = {.simple_preference = PROHIBIT}},
        [KEEP_ALIVE] = {.value = {.simple_preference = NO_PREFERENCE}},
        [INTERFACE] = {.value = {.simple_preference = NO_PREFERENCE}},
        [PVD] = {.value = {.simple_preference = NO_PREFERENCE}},
        [USE_TEMPORARY_LOCAL_ADDRESS] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTIPATH] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ADVERTISES_ALT_ADDRES] = {.value = {.simple_preference = NO_PREFERENCE}},
        [DIRECTION] = {.value = {.simple_preference = NO_PREFERENCE}},
        [SOFT_ERROR_NOTIFY] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ACTIVE_READ_BEFORE_SEND] = {.value = {.simple_preference = NO_PREFERENCE}},
      }
    },
    .init = udp_init,
    .init_with_send = udp_init_with_send,
    .send = udp_send,
    .listen = udp_listen,
    .stop_listen = udp_stop_listen,
    .close = udp_close,
    .abort = udp_abort,
    .clone_connection = udp_clone_connection,
    .remote_endpoint_from_peer = udp_remote_endpoint_from_peer,
    .free_state = udp_free_state,
    .free_connection_group_state = udp_free_connection_group_state,
};

// Used to free data in send callbac
typedef struct udp_send_data_s {
  ct_message_t* message;
  ct_message_context_t* message_context;
} udp_send_data_t;

udp_send_data_t* udp_send_data_new(ct_message_t* message, ct_message_context_t* message_context) {
  udp_send_data_t* send_data = malloc(sizeof(udp_send_data_t));
  if (!send_data) {
    log_error("Failed to allocate memory for UDP send data");
    return NULL;
  }
  send_data->message = message;
  send_data->message_context = message_context;
  return send_data;
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  (void)handle;
  *buf = uv_buf_init(malloc(suggested_size), suggested_size);
}

void udp_multiplex_received_message(ct_socket_manager_t* socket_manager, char* buf, size_t len, const struct sockaddr_storage* remote_addr) {
  log_trace("UDP listener received message, demultiplexing to connection");

  bool was_new = false;
  ct_connection_group_t* connection_group = socket_manager_get_connection_group(socket_manager, remote_addr);

  if (connection_group == NULL) {
    log_error("Failed to get or create connection group for UDP message");
    return;
  }

  // For UDP, get the first (and typically only) connection in the group
  ct_connection_t* connection = ct_connection_group_get_first(connection_group);
  if (connection == NULL) {
    log_error("Connection group exists but has no connections");
    return;
  }

  if (was_new) {
    log_debug("UDP listener invoking callback for new connection from remote endpoint");

    int rc = resolve_local_endpoint_from_handle((uv_handle_t*)socket_manager->internal_socket_manager_state, connection);
    if (rc < 0) {
      log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
    }
    socket_manager->listener->listener_callbacks.connection_received(socket_manager->listener, connection);
  }
  ct_connection_on_protocol_receive(connection, buf, len);
}

void on_send(uv_udp_send_t* req, int status) {
  if (status) {
    log_error("Send error: %s\n", uv_strerror(status));
  }
  if (req && req->data) {
    udp_send_data_t* send_data = (udp_send_data_t*)req->data;
    ct_message_free(send_data->message);
    ct_message_context_free(send_data->message_context);
    free(send_data);
  }
  free(req);
}

void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
             const struct sockaddr* addr, unsigned flags) {
  (void)flags;
  ct_connection_t* connection = (ct_connection_t*)handle->data;
  if (nread < 0) {
    log_error("Read error: %s\n", uv_strerror(nread));
    uv_close((uv_handle_t*)handle, NULL);
    free(buf->base);
    return;
  }

  if (addr == NULL) {
    // No more data to read, or an empty packet.
    if (buf->base) {
      free(buf->base);
    }
    return;
  }

  log_info("Received message over UDP handle");

  // Delegate to connection receive handler (handles framing if present)
  ct_connection_on_protocol_receive(connection, buf->base, nread);
  free(buf->base);
}

void abort_handle_cb(uv_handle_t* handle) {
  log_info("UDP handle abort callback invoked with handle: %p", handle);
  ct_connection_t* connection = (ct_connection_t*)handle->data;
  log_info("Connection pointer in abort callback: %p", connection);
  if (connection) {
    ct_connection_mark_as_closed(connection);
    if (connection->connection_callbacks.connection_error) {
      connection->connection_callbacks.connection_error(connection);
    }
    else {
      log_warn("No connection error callback set for UDP connection");
    }
  }
  free(handle);
}

void closed_handle_cb(uv_handle_t* handle) {
  log_info("UDP handle closed callback invoked with handle: %p", handle);
  ct_connection_t* connection = (ct_connection_t*)handle->data;
  if (connection) {
    ct_connection_mark_as_closed(connection);
    if (connection->connection_callbacks.closed) {
      log_trace("Invoking UDP connection closed callback");
      connection->connection_callbacks.closed(connection);
    }
  }
}

int udp_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context) {
  (void)connection_callbacks;
  log_debug("Initiating UDP connection\n");

  uv_udp_t* new_udp_handle = create_udp_listening_on_local(connection->local_endpoint, alloc_buffer, on_read);
  if (!new_udp_handle) {
    log_error("Failed to create UDP handle for connection");
    return -EIO;
  }

  
  int rc = resolve_local_endpoint_from_handle((uv_handle_t*)new_udp_handle, connection);
  if (rc < 0) {
    log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
    uv_close((uv_handle_t*)new_udp_handle, closed_handle_cb);
    return rc;
  }

  // Store in internal connection state instead of connection group,
  // because UDP does not have a multiplexing concept, so when cloning
  // each connection gets its own handle (or is multiplexed)
  connection->internal_connection_state = (uv_handle_t*)new_udp_handle;
  new_udp_handle->data = connection;

  ct_connection_mark_as_established(connection);

  if (initial_message) {
    udp_send(connection, initial_message, initial_message_context);
  }

  if (connection->connection_callbacks.ready) {
    connection->connection_callbacks.ready(connection);
  }
  else {
    log_warn("No ready callback set for UDP connection");
  }
  return 0;
}

int udp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks) {
  return udp_init_with_send(connection, connection_callbacks, NULL, NULL);
}

int udp_close(ct_connection_t* connection) {
  log_info("Closing UDP connection");

  if (connection->internal_connection_state) {
    log_debug("Stopping UDP receive and closing handle");
    uv_udp_recv_stop((uv_udp_t*)connection->internal_connection_state);
    uv_close(connection->internal_connection_state, closed_handle_cb);
  }

  return 0;
}

void udp_abort(ct_connection_t* connection) {
  log_info("Aborting UDP connection");

  if (connection->internal_connection_state) {
    log_debug("Stopping UDP receive and aborting handle");
    uv_udp_recv_stop((uv_udp_t*)connection->internal_connection_state);
    uv_close(connection->internal_connection_state, abort_handle_cb);
  }
}

int udp_stop_listen(struct ct_socket_manager_s* socket_manager) {
  log_debug("Stopping UDP listen");
  int rc = uv_udp_recv_stop((uv_udp_t*)socket_manager->internal_socket_manager_state);
  if (rc < 0) {
    log_error("Problem with stopping receive: %s\n", uv_strerror(rc));
    return rc;
  }
  return 0;
}

int udp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context) {
  (void)message_context;
  log_debug("Sending message over UDP");

  // Use the message content directly as the send buffer (it's already heap-allocated)
  uv_buf_t buffer = uv_buf_init(message->content, message->length);

  uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
  if (!send_req) {
    log_error("Failed to allocate send request\n");
    ct_message_free(message);
    return -ENOMEM;
  }

  // Store the message in send_req->data so we can free it in the callback
  send_req->data = udp_send_data_new(message, message_context);

  int rc = uv_udp_send(
      send_req, (uv_udp_t*)connection->internal_connection_state, &buffer, 1,
      (const struct sockaddr*)&connection->remote_endpoint->data.resolved_address,
      on_send);

  if (rc < 0) {
    log_error("Error sending UDP message: %s", uv_strerror(rc));
    ct_message_free(message);
    free(send_req);
  }

  return rc;
}

void socket_listen_callback(uv_udp_t* handle,
                               ssize_t nread,
                               const uv_buf_t* buf,
                               const struct sockaddr* addr,
                               unsigned flags) {
  (void)flags;
  if (nread == 0 && addr == NULL) {
    // No more data to read, or an empty packet.
    log_info("Socket listen callback invoked, but nothing to read from udp socket or empty packet");
    if (buf->base) {
      free(buf->base);
    }
    return;
  }

  if (nread < 0) {
    log_error("Read error in socket_listen_callback: %s\n", uv_strerror(nread));
    free(buf->base);
    return;
  }


  ct_socket_manager_t *socket_manager = (ct_socket_manager_t*)handle->data;

  udp_multiplex_received_message(socket_manager, buf->base, (size_t)nread, (struct sockaddr_storage*)addr);
  // When buf is passed up to connection, connection.c copies the content into a message, so
  // we can safely free the buffer here
  free(buf->base);
}

int udp_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Listening via UDP");

  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(socket_manager->listener);
  uv_udp_t* udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_buffer, socket_listen_callback);
  if (udp_handle == NULL) {
    log_error("Failed to create UDP handle for listening");
    return -EIO;
  }

  udp_handle->data = socket_manager;
  socket_manager_increment_ref(socket_manager);
  socket_manager->internal_socket_manager_state = (uv_handle_t*)udp_handle;

  return 0;
}

int udp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer) {
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  int rc = uv_udp_getpeername((uv_udp_t*)peer, (struct sockaddr *)&remote_addr, &addr_len);
  if (rc < 0) {
    log_error("Could not get remote address from received handle: %s", uv_strerror(rc));
    return rc;
  }
  rc = ct_remote_endpoint_from_sockaddr(resolved_peer, &remote_addr);
  if (rc < 0) {
    log_error("Could not build remote endpoint from received handle's remote address");
    return rc;
  }
  return 0;
}

int udp_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection) {
  if (!source_connection || !target_connection) {
    log_error("Source or target connection is NULL in udp_clone_connection");
    return -EINVAL;
  }
  // Create ephemeral local port
  uv_udp_t* new_udp_handle = create_udp_listening_on_ephemeral(alloc_buffer, on_read);

  target_connection->internal_connection_state = (uv_handle_t*)new_udp_handle;
  new_udp_handle->data = target_connection;

  int rc = resolve_local_endpoint_from_handle((uv_handle_t*)new_udp_handle, target_connection);
  if (rc < 0) {
    log_error("Failed to get UDP socket name for cloned connection: %s", uv_strerror(rc));
    uv_close((uv_handle_t*)new_udp_handle, closed_handle_cb);
    return rc;
  }

  ct_connection_mark_as_established(target_connection);
  if (target_connection->connection_callbacks.ready) {
    target_connection->connection_callbacks.ready(target_connection);
  }

  return 0;
}

int udp_free_state(ct_connection_t* connection) {
  return 0; // Fix after ownership refactor
  log_trace("Freeing UDP connection resources");
  if (!connection || !connection->internal_connection_state) {
    log_warn("UDP connection or internal state is NULL during free_state");
    log_debug("Connection pointer: %p", (void*)connection);
    if (connection) {
      log_debug("Internal connection state pointer: %p", (void*)connection->internal_connection_state);
    }
    return -EINVAL;
  }
  uv_udp_t* handle = (uv_udp_t*)connection->internal_connection_state;
  free(handle);
  return 0;
}

int udp_free_connection_group_state(ct_connection_group_t* connection_group) {
  (void)connection_group;
  return 0;
}
