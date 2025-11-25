#include "udp.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>
#include <logging/log.h>
#include <errno.h>
#include <protocol/common/socket_utils.h>

#include "ctaps.h"

#include "connection/connection.h"
#include "connection/socket_manager/socket_manager.h"

#define MAX_FOUND_INTERFACE_ADDRS 64

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  *buf = uv_buf_init(malloc(suggested_size), suggested_size);
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

  connection_callbacks->ready(connection);
  return 0;
}

void closed_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle");
}

int udp_close(ct_connection_t* connection) {
  log_info("Closing UDP connection");

  if (connection->socket_type == CONNECTION_SOCKET_TYPE_MULTIPLEXED) {
    log_info("Closing multiplexed UDP connection, removing from socket manager");
    int rc = socket_manager_remove_connection(connection->socket_manager, (ct_connection_t*)connection);
    if (rc < 0) {
      log_error("Error removing UDP connection from socket manager: %d", rc);
      return rc;
    }
  } else {
    // Standalone connection - close the UDP handle
    if (connection->internal_connection_state) {
      uv_udp_recv_stop((uv_udp_t*)connection->internal_connection_state);
      uv_close(connection->internal_connection_state, closed_handle_cb);
    }
  }

  ct_connection_mark_as_closed(connection);

  return 0;
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
  received_message->content = malloc(nread);
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

  socket_manager_multiplex_received_message(socket_manager, received_message, (struct sockaddr_storage*)addr);
}

int udp_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Listening via UDP");
  int rc;

  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(socket_manager->listener);
  uv_udp_t* udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_buffer, socket_listen_callback);
  if (udp_handle == NULL) {
    log_error("Failed to create UDP handle for listening");
    return -EIO;
  }

  ct_listener_t* listener = socket_manager->listener;

  udp_handle->data = socket_manager;
  socket_manager_increment_ref(socket_manager);
  socket_manager->internal_socket_manager_state = (uv_handle_t*)udp_handle;

  return 0;
}

int udp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer) {
  int rc;
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  rc = uv_udp_getpeername((uv_udp_t*)peer, (struct sockaddr *)&remote_addr, &addr_len);
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
