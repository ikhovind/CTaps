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
    .send = udp_send,
    .listen = udp_listen,
    .stop_listen = udp_stop_listen,
    .close = udp_close,
    .abort = udp_abort,
    .clone_connection = udp_clone_connection,
    .remote_endpoint_from_peer = udp_remote_endpoint_from_peer,
    .retarget_protocol_connection = udp_retarget_protocol_connection
};

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  (void)handle;
  *buf = uv_buf_init(malloc(suggested_size), suggested_size);
}

void udp_multiplex_received_message(ct_socket_manager_t* socket_manager, ct_message_t* message, const struct sockaddr_storage* remote_addr) {
  log_trace("UDP listener received message, demultiplexing to connection");

  bool was_new = false;
  ct_connection_group_t* connection_group = socket_manager_get_or_create_connection_group(
      socket_manager, remote_addr, &was_new);

  if (connection_group == NULL) {
    log_error("Failed to get or create connection group for UDP message");
    ct_message_free_all(message);
    return;
  }

  // For UDP, get the first (and typically only) connection in the group
  ct_connection_t* connection = ct_connection_group_get_first(connection_group);
  if (connection == NULL) {
    log_error("Connection group exists but has no connections");
    ct_message_free_all(message);
    return;
  }

  if (was_new) {
    log_debug("UDP listener invoking callback for new connection from remote endpoint");
    socket_manager->listener->listener_callbacks.connection_received(socket_manager->listener, connection);
  }

  if (g_queue_is_empty(connection->received_callbacks)) {
    log_debug("Connection has no receive callback ready, queueing message");
    g_queue_push_tail(connection->received_messages, message);
  }
  else {
    log_debug("Connection has receive callback ready, invoking it");
    ct_receive_callbacks_t* receive_callback = g_queue_pop_head(connection->received_callbacks);

    ct_message_context_t ctx = {0};
    ctx.user_receive_context = receive_callback->user_receive_context;
    receive_callback->receive_callback(connection, &message, &ctx);
    free(receive_callback);
  }
}

void on_send(uv_udp_send_t* req, int status) {
  if (status) {
    log_error("Send error: %s\n", uv_strerror(status));
  }
  if (req && req->data) {
    ct_message_t* message = (ct_message_t*)req->data;
    ct_message_free_all(message);
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

int udp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks) {
  log_debug("Initiating UDP connection\n");

  uv_udp_t* new_udp_handle = create_udp_listening_on_local(&connection->local_endpoint, alloc_buffer, on_read);
  if (!new_udp_handle) {
    log_error("Failed to create UDP handle for connection");
    return -EIO;
  }

  // Store in internal connection state instead of connection group,
  // because UDP does not have a multiplexing concept, so when cloning
  // each connection gets its own handle (or is multiplexed)
  connection->internal_connection_state = (uv_handle_t*)new_udp_handle;
  new_udp_handle->data = connection;

  ct_connection_mark_as_established(connection);
  connection_callbacks->ready(connection);
  return 0;
}

void closed_handle_cb(uv_handle_t* handle) {
  (void)handle;
  log_info("Successfully closed UDP handle");
}

int udp_close(ct_connection_t* connection) {
  log_info("Closing UDP connection");

  if (connection->socket_type == CONNECTION_SOCKET_TYPE_MULTIPLEXED) {
    log_info("Closing multiplexed UDP connection");

    ct_connection_group_t* connection_group = connection->connection_group;

    // Decrement active connection counter and mark as closed
    ct_connection_group_decrement_active(connection_group);
    ct_connection_mark_as_closed(connection);

    // If no more active connections in group, remove group from socket manager
    if (ct_connection_group_get_num_active_connections(connection_group) == 0) {
      log_info("No more active connections in group, removing from socket manager");
      int rc = socket_manager_remove_connection_group(
          connection->socket_manager,
          &connection->remote_endpoint.data.resolved_address);
      if (rc < 0) {
        log_error("Could not find connection group in socket manager: %d", rc);
        return rc;
      }
    }
  } else {
    // Standalone connection - close the UDP handle
    if (connection->internal_connection_state) {
      uv_udp_recv_stop((uv_udp_t*)connection->internal_connection_state);
      uv_close(connection->internal_connection_state, closed_handle_cb);
    }
    ct_connection_mark_as_closed(connection);
  }

  return 0;
}

void udp_abort(ct_connection_t* connection) {
  log_info("Aborting UDP connection");

  if (connection->socket_type == CONNECTION_SOCKET_TYPE_MULTIPLEXED) {
    log_info("Aborting multiplexed UDP connection");

    ct_connection_group_t* connection_group = connection->connection_group;

    // Decrement active connection counter and mark as closed
    ct_connection_group_decrement_active(connection_group);
    ct_connection_mark_as_closed(connection);

    // If no more active connections in group, remove group from socket manager
    if (ct_connection_group_get_num_active_connections(connection_group) == 0) {
      log_info("No more active connections in group, removing from socket manager");
      int rc = socket_manager_remove_connection_group(
          connection->socket_manager,
          &connection->remote_endpoint.data.resolved_address);
      if (rc < 0) {
        log_error("Could not find connection group in socket manager: %d", rc);
      }
    }
  } else {
    // Standalone connection - close the UDP handle
    uv_udp_recv_stop((uv_udp_t*)connection->internal_connection_state);
    uv_close(connection->internal_connection_state, closed_handle_cb);
    ct_connection_mark_as_closed(connection);
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
    ct_message_free_all(message);
    return -ENOMEM;
  }

  // Store the message in send_req->data so we can free it in the callback
  send_req->data = message;

  int rc = uv_udp_send(
      send_req, (uv_udp_t*)connection->internal_connection_state, &buffer, 1,
      (const struct sockaddr*)&connection->remote_endpoint.data.resolved_address,
      on_send);

  if (rc < 0) {
    log_error("Error sending UDP message: %s", uv_strerror(rc));
    ct_message_free_all(message);
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

  ct_message_t* received_message = malloc(sizeof(ct_message_t));
  if (!received_message) {
    free(buf->base);
    return;
  }
  if (nread > 0) {
    received_message->content = malloc(nread);
  }
  else {
    received_message->content = NULL;
  }
  if (!received_message->content) {
    free(received_message);
    free(buf->base);
    log_error("Could not allocate memory for received message content");
    return;
  }
  received_message->length = nread;

  memcpy(received_message->content, buf->base, nread);

  // Free the buffer allocated by alloc_buffer now that we've copied the data
  free(buf->base);

  udp_multiplex_received_message(socket_manager, received_message, (struct sockaddr_storage*)addr);
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

void udp_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection) {
  // For UDP, internal_connection_state is the uv_udp_t handle directly
  // Update the handle's data pointer to reference the new connection
  if (from_connection->internal_connection_state) {
    uv_handle_t* handle = (uv_handle_t*)from_connection->internal_connection_state;
    handle->data = to_connection;
  }
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

  ct_connection_mark_as_established(target_connection);
  if (target_connection->connection_callbacks.ready) {
    target_connection->connection_callbacks.ready(target_connection);
  }

  return 0;
}
