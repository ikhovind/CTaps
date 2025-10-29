#include "udp.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>
#include <connections/listener/listener.h>
#include <logging/log.h>
#include <errno.h>
#include <protocols/common/socket_utils.h>

#include "connections/connection/connection.h"
#include "connections/connection/connection_callbacks.h"
#include "connections/listener/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "message/message_context/message_context.h"
#include "protocols/registry/protocol_registry.h"

#define MAX_FOUND_INTERFACE_ADDRS 64

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  // We'll use a static buffer for this simple example, but in a real
  // application, you would likely use malloc or a buffer pool.
  static char slab[65536];
  *buf = uv_buf_init(slab, sizeof(slab));
}

void on_send(uv_udp_send_t* req, int status) {
  if (status) {
    log_error("Send error: %s\n", uv_strerror(status));
  }
  if (req) {
    free(req);  // Free the send request
  }
}

void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
             const struct sockaddr* addr, unsigned flags) {
  Connection* connection = (Connection*)handle->data;
  if (nread < 0) {
    log_error("Read error: %s\n", uv_strerror(nread));
    uv_close((uv_handle_t*)handle, NULL);
    free(buf->base);
    return;
  }

  if (addr == NULL) {
    // No more data to read, or an empty packet.
    return;
  }

  Message* received_message = malloc(sizeof(Message));
  if (!received_message) {
    log_error("Failed to allocate send request\n");
    return;
  }
  received_message->content = malloc(nread);
  if (!received_message->content) {
    log_error("Failed to allocate message content\n");
    return;
  }
  received_message->length = nread;

  memcpy(received_message->content, buf->base, nread);

  if (g_queue_is_empty(connection->received_callbacks)) {
    log_debug("No receive callback ready, queueing message");
    g_queue_push_tail(connection->received_messages, received_message);
  }

  else {
    log_debug("Receive callback ready, calling it");
    ReceiveCallbacks* receive_callback =
        g_queue_pop_head(connection->received_callbacks);

    receive_callback->receive_callback(connection, &received_message, NULL, receive_callback->user_data);
    free(receive_callback);
  }
}

int udp_init(Connection* connection, const ConnectionCallbacks* connection_callbacks) {
  log_debug("Initiating UDP connection\n");

  uv_udp_t* new_udp_handle = create_udp_listening_on_local(&connection->local_endpoint, alloc_buffer, on_read);
  if (!new_udp_handle) {
    log_error("Failed to create UDP handle for connection");
    return -EIO;
  }

  connection->protocol_state = (uv_handle_t*)new_udp_handle;
  new_udp_handle->data = connection;

  connection_callbacks->ready(connection, connection_callbacks->user_data);

  log_trace("Successfully initiated UDP connection");
  return 0;
}

void closed_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle");
}

int udp_close(const Connection* connection) {
  g_queue_free(connection->received_messages);
  g_queue_free(connection->received_callbacks);
  uv_udp_recv_stop((uv_udp_t*)connection->protocol_state);
  uv_close(connection->protocol_state, closed_handle_cb);
  return 0;
}

int udp_stop_listen(struct SocketManager* socket_manager) {
  log_debug("Stopping UDP listen");
  int rc = uv_udp_recv_stop((uv_udp_t*)socket_manager->protocol_state);
  if (rc < 0) {
    log_error("Problem with stopping receive: %s\n", uv_strerror(rc));
    return rc;
  }
  return 0;
}

int udp_send(Connection* connection, Message* message, MessageContext* message_context) {
  log_debug("Sending message over UDP");
  const uv_buf_t buffer =
      uv_buf_init(message->content, message->length);

  uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
  if (!send_req) {
    log_error("Failed to allocate send request\n");
    return -ENOMEM;
  }

  return uv_udp_send(
      send_req, (uv_udp_t*)connection->protocol_state, &buffer, 1,
      (const struct sockaddr*)&connection->remote_endpoint.data.resolved_address,
      on_send);
}

int udp_receive(Connection* connection, ReceiveCallbacks receive_callbacks) {
  // If we have a message to receive then simply return that
  log_debug("UDP receiving");
  if (!g_queue_is_empty(connection->received_messages)) {
    log_debug("Calling receive callback immediately");
    Message* received_message = g_queue_pop_head(connection->received_messages);
    receive_callbacks.receive_callback(connection, &received_message, NULL, receive_callbacks.user_data);
    return 0;
  }

  ReceiveCallbacks* ptr = malloc(sizeof(ReceiveCallbacks));
  memcpy(ptr, &receive_callbacks, sizeof(ReceiveCallbacks));

  // If we don't have a message to receive, add the callback to the queue of
  // waiting callbacks
  g_queue_push_tail(connection->received_callbacks, ptr);
  return 0;
}

void socket_listen_callback(uv_udp_t* handle,
                               ssize_t nread,
                               const uv_buf_t* buf,
                               const struct sockaddr* addr,
                               unsigned flags) {
  if (nread == 0 && addr == NULL) {
    // No more data to read, or an empty packet.
    log_info("Socket listen callback invoked, but nothing to read from udp socket or empty packet");
    return;
  }
  SocketManager *socket_manager = (SocketManager*)handle->data;

  Message* received_message = malloc(sizeof(Message));
  if (!received_message) {
    return;
  }
  received_message->content = malloc(nread);
  if (!received_message->content) {
    free(received_message);
    log_error("Could not allocate memory for received message content");
    return;
  }
  received_message->length = nread;

  memcpy(received_message->content, buf->base, nread);

  socket_manager_multiplex_received_message(socket_manager, received_message, (struct sockaddr_storage*)addr);
}

int udp_listen(SocketManager* socket_manager) {
  log_debug("Listening via UDP");
  int rc;

  LocalEndpoint local_endpoint = listener_get_local_endpoint(socket_manager->listener);
  uv_udp_t* udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_buffer, socket_listen_callback);
  if (udp_handle == NULL) {
    log_error("Failed to create UDP handle for listening");
    return -EIO;
  }

  Listener* listener = socket_manager->listener;

  udp_handle->data = socket_manager;
  socket_manager_increment_ref(socket_manager);
  socket_manager->protocol_state = (uv_handle_t*)udp_handle;

  return 0;
}

int udp_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer) {
  int rc;
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  rc = uv_udp_getpeername((uv_udp_t*)peer, (struct sockaddr *)&remote_addr, &addr_len);
  if (rc < 0) {
    log_error("Could not get remote address from received handle: %s", uv_strerror(rc));
    return rc;
  }
  rc = remote_endpoint_from_sockaddr(resolved_peer, &remote_addr);
  if (rc < 0) {
    log_error("Could not build remote endpoint from received handle's remote address");
    return rc;
  }
  return 0;
}
