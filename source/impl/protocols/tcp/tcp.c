#include "tcp.h"
#include "connections/connection/connection.h"
#include "ctaps.h"
#include <errno.h>
#include <logging/log.h>
#include <stdbool.h>
#include <uv.h>

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
	*buf = uv_buf_init(malloc(size), size);
}

void on_close(uv_handle_t* handle) {
  free(handle);
}

void tcp_on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  Connection* connection = (Connection*)handle->data;
  if (nread < 0) {
    log_error("Read error: %s\n", uv_strerror(nread));
    connection_close(connection);
    free(buf->base);
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
    ReceiveCallbacks* receive_callback = g_queue_pop_head(connection->received_callbacks);

    receive_callback->receive_callback(connection, &received_message, NULL, receive_callback->user_data);
    free(receive_callback);
  }
}

void on_connect(struct uv_connect_s *req, int status) {
  Connection* connection = (Connection*)req->handle->data;
  if (status < 0) {
    log_error("Connection error: %s", uv_strerror(status));
    connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection, connection->connection_callbacks.user_data);
    }
    free(req);
    return;
  }
  log_info("Successfully connected to remote endpoint using TCP");
  uv_read_start(connection->protocol_uv_handle, alloc_cb, tcp_on_read);
  if (connection->connection_callbacks.ready) {
    connection->connection_callbacks.ready(connection, connection->connection_callbacks.user_data);
  }
}

void on_write(uv_write_t* req, int status) {
  Connection *connection = (Connection*)req->handle->data;
  if (status < 0) {
    log_error("Write error: %s", uv_strerror(status));
    if (connection->connection_callbacks.send_error) {
      connection->connection_callbacks.send_error(connection, connection->connection_callbacks.user_data);
    }
    return;
  }
  if (connection->connection_callbacks.sent) {
    connection->connection_callbacks.sent(connection, connection->connection_callbacks.user_data);
  }
  log_info("Successfully sent message over TCP");
}

int tcp_init(Connection* connection, const ConnectionCallbacks* connection_callbacks) {
  int rc;
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    if (errno == 0) {
      return -ENOMEM;
    }
    return errno;
  }

  connection->protocol_uv_handle = (uv_handle_t*)new_tcp_handle;

  rc = uv_tcp_init(ctaps_event_loop, new_tcp_handle);

  if (rc < 0) {
    log_error( "Error initializing udp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }
  new_tcp_handle->data = connection;

  uint32_t keepalive_timeout = connection->transport_properties.connection_properties.list[KEEP_ALIVE_TIMEOUT].value.uint32_val;
  if (keepalive_timeout != CONN_TIMEOUT_DISABLED) {
    rc = uv_tcp_keepalive(new_tcp_handle, true, keepalive_timeout);
    if (rc < 0) {
      log_warn("Error setting TCP keepalive: %s", uv_strerror(rc));
    }
  }

  uv_connect_t* connect_req = malloc(sizeof(uv_connect_t));
  rc = uv_tcp_connect(connect_req,
                 new_tcp_handle,
                 (const struct sockaddr*)&connection->remote_endpoint.data.resolved_address,
                 on_connect);
  if (rc < 0) {
    log_error("Error initiating TCP connection: %s", uv_strerror(rc));
    free(connect_req);
    connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection, connection->connection_callbacks.user_data);
    }
  }
  
  return 0;
}

int tcp_close(const Connection* connection) {
  log_info("Closing TCP connection");
  if (connection->protocol_uv_handle) {
    uv_close(connection->protocol_uv_handle, on_close);
  }
  return 0;
}

int tcp_send(Connection* connection, Message* message, MessageContext* ctx) {
  uv_buf_t buffer[] = {
    {
      .base = message->content,
      .len = message->length, 
    }
  };
  uv_write_t *req = malloc(sizeof(uv_write_t));
  if (!req) {
    log_error("Failed to allocate memory for write request");
    return -errno;
  }
  int rc = uv_write(req, (uv_tcp_t*)connection->protocol_uv_handle, buffer, 1, on_write);
  if (rc < 0) {
    log_error("Error sending message over TCP: %s", uv_strerror(rc));
    free(req);
    if (connection->connection_callbacks.send_error) {
      connection->connection_callbacks.send_error(connection, connection->connection_callbacks.user_data);
    }

    return rc;
  }
  return 0;
}
int tcp_receive(Connection* connection, ReceiveCallbacks receive_callbacks) {
  // If we have a message to receive then simply return that
  log_debug("TCP receiving");
  if (!g_queue_is_empty(connection->received_messages)) {
    log_debug("Calling receive callback immediately");
    Message* received_message = g_queue_pop_head(connection->received_messages);
    // TODO is returning &received_message safe here?
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

int tcp_listen(struct SocketManager* socket_manager) {
  return -ENOSYS;
}

int tcp_stop_listen(struct SocketManager* listener) {
  return -ENOSYS;
}
