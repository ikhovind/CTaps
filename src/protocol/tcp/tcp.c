#include "tcp.h"
#include "ctaps.h"
#include "connection/socket_manager/socket_manager.h"
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
  ct_connection_t* connection = (ct_connection_t*)handle->data;
  log_info("TCP received message for ct_connection_t: %p", connection);
  if (nread < 0) {
    log_error("Read error: %s\n", uv_strerror(nread));
    ct_connection_close(connection);
    free(buf->base);
    return;
  }

  // Delegate to connection receive handler (handles framing if present)
  ct_connection_on_protocol_receive(connection, buf->base, nread);
  free(buf->base);
}

void on_connect(struct uv_connect_s *req, int status) {
  ct_connection_t* connection = (ct_connection_t*)req->handle->data;
  if (status < 0) {
    log_error("ct_connection_t error: %s", uv_strerror(status));
    ct_connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection);
    }
    free(req);
    return;
  }
  log_info("Successfully connected to remote endpoint using TCP");
  uv_read_start((uv_stream_t*)connection->internal_connection_state, alloc_cb, tcp_on_read);
  if (connection->connection_callbacks.ready) {
    connection->connection_callbacks.ready(connection);
  }
}

void on_write(uv_write_t* req, int status) {
  ct_connection_t *connection = (ct_connection_t*)req->handle->data;
  ct_message_t* message = (ct_message_t*)req->data;

  if (status < 0) {
    log_error("Write error: %s", uv_strerror(status));
    if (connection->connection_callbacks.send_error) {
      connection->connection_callbacks.send_error(connection);
    }
  } else {
    if (connection->connection_callbacks.sent) {
      connection->connection_callbacks.sent(connection);
    }
    log_info("Successfully sent message over TCP");
  }

  // Free the message after sending (or error)
  if (message) {
    log_debug("Freeing sent message");
    ct_message_free_all(message);
    log_debug("Sent message freed");
  }
  log_debug("Freeing write request");
  free(req);
}

int tcp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks) {
  int rc;
  log_info("Initiating TCP connection");
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  // Store in internal connection state instead of connection group,
  // because TCP does not have a multiplexing concept, so when cloning
  // each connection gets its own handle
  connection->internal_connection_state = (uv_handle_t*)new_tcp_handle;

  rc = uv_tcp_init(event_loop, new_tcp_handle);

  if (rc < 0) {
    log_error("Error initializing udp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }
  new_tcp_handle->data = connection;

  uint32_t keepalive_timeout = connection->transport_properties.connection_properties.list[KEEP_ALIVE_TIMEOUT].value.uint32_val;
  if (keepalive_timeout != CONN_TIMEOUT_DISABLED) {
    log_info("Setting TCP keepalive with timeout: %u seconds", keepalive_timeout);
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
    ct_connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection);
    }
    return rc;
  }
  return 0;
}

int tcp_close(const ct_connection_t* connection) {
  log_info("Closing TCP connection");

  if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
    log_info("Closing multiplexed TCP connection, removing from socket manager");
    int rc = socket_manager_remove_connection(connection->socket_manager, (ct_connection_t*)connection);
    if (rc < 0) {
      log_error("Error removing TCP connection from socket manager: %d", rc);
      return rc;
    }
  } else {
    // Standalone connection - close the TCP handle
    if (connection->internal_connection_state) {
      uv_close((uv_handle_t*)connection->internal_connection_state, on_close);
    }
  }

  ((ct_connection_t*)connection)->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;

  return 0;
}

int tcp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* ctx) {
  log_debug("Sending message over TCP");

  uv_buf_t buffer = uv_buf_init(message->content, message->length);

  uv_write_t *req = malloc(sizeof(uv_write_t));
  if (!req) {
    log_error("Failed to allocate memory for write request");
    return -errno;
  }

  // Attach message to request so it can be freed in the callback
  req->data = message;

  uv_tcp_t* tcp_handle = (uv_tcp_t*)connection->internal_connection_state;
  int rc = uv_write(req, (uv_stream_t*)tcp_handle, &buffer, 1, on_write);
  if (rc < 0) {
    log_error("Error sending message over TCP: %s", uv_strerror(rc));
    free(req);
    ct_message_free_all(message);
    return rc;
  }
  return 0;
}

int tcp_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Listening via TCP");
  int rc;
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  ct_listener_t* listener = socket_manager->listener;

  rc = uv_tcp_init(event_loop, new_tcp_handle);
  if (rc < 0) {
    log_error( "Error initializing tcp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }


  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(listener);
  rc = uv_tcp_bind(new_tcp_handle, (const struct sockaddr*)&local_endpoint.data.address, 0);
  if (rc < 0) {
    log_error("Error binding TCP handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  rc = uv_listen((uv_stream_t*)new_tcp_handle, SOMAXCONN, new_stream_connection_cb);

  if (rc < 0) {
    log_error("Error starting TCP listen: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  socket_manager_increment_ref(socket_manager);
  socket_manager->internal_socket_manager_state = (uv_handle_t*)new_tcp_handle;
  new_tcp_handle->data = listener;

  return 0;
}

void new_stream_connection_cb(uv_stream_t *server, int status) {
  log_debug("New TCP connection received for ct_listener_t");
  int rc;
  if (status < 0) {
    log_error("New connection error: %s", uv_strerror(status));
    return;
  }
  uv_tcp_t *client = malloc(sizeof(uv_tcp_t));
  if (!client) {
    log_error("Failed to allocate memory for new TCP client");
    return;
  }
  rc = uv_tcp_init(event_loop, client);
  if (rc < 0) {
    log_error("Error initializing TCP client handle: %s", uv_strerror(rc));
    free(client);
    return;
  }

  ct_listener_t* listener = server->data;

  rc = uv_accept(server, (uv_stream_t*)client);
  if (rc < 0) {
    log_error("Error accepting new TCP connection: %s", uv_strerror(rc));
    uv_close((uv_handle_t*)client, on_close);
    return;
  }

  ct_connection_t* connection = ct_connection_build_from_received_handle(listener, (uv_stream_t*)client);

  if (!connection) {
    log_error("Failed to build connection from received handle");
    uv_close((uv_handle_t*)client, on_close);
    return;
  }

  client->data = connection;

  rc = uv_read_start((uv_stream_t*)client, alloc_cb, tcp_on_read);
  if (rc < 0) {
    log_error("Could not start reading from TCP connection: %s", uv_strerror(rc));
    uv_close((uv_handle_t*)client, on_close);
    ct_connection_close(connection);
    free(connection);
    return;
  }

  log_trace("TCP invoking new connection callback");
  listener->listener_callbacks.connection_received(listener, connection);
}

int tcp_stop_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Stopping TCP listen for ct_socket_manager_t %p", (void*)socket_manager);

  if (socket_manager->internal_socket_manager_state) {
    uv_close((uv_handle_t*)socket_manager->internal_socket_manager_state, on_close);
    socket_manager->internal_socket_manager_state = NULL;
  }
  return 0;
}

int tcp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer) {
  int rc;
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  rc = uv_tcp_getpeername((uv_tcp_t*)peer, (struct sockaddr *)&remote_addr, &addr_len);
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

void tcp_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection) {
  // For TCP, internal_connection_state is the uv_tcp_t handle directly
  // Update the handle's data pointer to reference the new connection
  if (from_connection->internal_connection_state) {
    uv_handle_t* handle = (uv_handle_t*)from_connection->internal_connection_state;
    handle->data = to_connection;
  }
}
