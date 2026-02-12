#include "tcp.h"
#include "ctaps.h"

#include "connection/connection.h"
#include "connection/connection_group.h"
#include "endpoint/remote_endpoint.h"
#include "endpoint/local_endpoint.h"
#include "protocol/common/socket_utils.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <errno.h>
#include <logging/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <uv.h>

// Protocol interface definition (moved from header to access internal struct)
const ct_protocol_impl_t tcp_protocol_interface = {
    .name = "TCP",
    .protocol_enum = CT_PROTOCOL_TCP,
    .supports_alpn = false,
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = PROHIBIT}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = REQUIRE}},
        [ZERO_RTT_MSG] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTISTREAMING] = {.value = {.simple_preference = PROHIBIT}},
        [FULL_CHECKSUM_SEND] = {.value = {.simple_preference = REQUIRE}},
        [FULL_CHECKSUM_RECV] = {.value = {.simple_preference = REQUIRE}},
        [CONGESTION_CONTROL] = {.value = {.simple_preference = REQUIRE}},
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
    .init = tcp_init,
    .init_with_send = tcp_init_with_send,
    .send = tcp_send,
    .listen = tcp_listen,
    .stop_listen = tcp_stop_listen,
    .close = tcp_close,
    .close_socket = tcp_close_socket,
    .abort = tcp_abort,
    .clone_connection = tcp_clone_connection,
    .remote_endpoint_from_peer = tcp_remote_endpoint_from_peer,
    .free_connection_state = tcp_free_state,
    .free_connection_group_state = tcp_free_connection_group_state,
};

// Per-socket TCP state
// Since every TCP connection has its own socket, tcp has no other protocol state
typedef struct ct_tcp_socket_state_s {
  ct_connection_t* connection;
  ct_listener_t* listener;
  ct_message_t* initial_message;
  ct_message_context_t* initial_message_context;
  uv_connect_t* connect_req; // To be freed in tests etc. when we don't run the full connect flow
  ct_on_connection_close_cb close_cb; // not a pointer because the typedef is for a pointer
  uv_tcp_t* tcp_handle;
} ct_tcp_socket_state_t;

ct_tcp_socket_state_t* ct_tcp_socket_state_new(ct_connection_t* connection,
                                                 ct_listener_t* listener,
                                                 ct_message_t* initial_message,
                                                 ct_message_context_t* initial_message_context,
                                                 uv_connect_t* connect_req,
                                                 uv_tcp_t* uv_tcp_handle
                                                 ) {
  ct_tcp_socket_state_t* socket_state = malloc(sizeof(ct_tcp_socket_state_t));
  if (!socket_state) {
    log_error("Failed to allocate memory for TCP connection state");
    return NULL;
  }
  memset(socket_state, 0, sizeof(ct_tcp_socket_state_t));
  socket_state->connection = connection;
  socket_state->listener = listener;
  socket_state->initial_message = initial_message;
  socket_state->initial_message_context = initial_message_context;
  socket_state->connect_req = connect_req;
  socket_state->tcp_handle = uv_tcp_handle;
  return socket_state;
}

void tcp_connection_state_free(ct_tcp_socket_state_t* socket_state) {
  if (!socket_state) {
    log_warn("Attempted to free NULL TCP connection state");
  }
  if (socket_state->connect_req) {
    free(socket_state->connect_req);
  }
  if (socket_state->initial_message_context) {
    ct_message_context_free(socket_state->initial_message_context);
  }
  if (socket_state->tcp_handle) {
    free(socket_state->tcp_handle);
  }
  free(socket_state);
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
	(void)handle;
	*buf = uv_buf_init(malloc(size), size);
}

void on_abort(uv_handle_t* handle) {
  ct_socket_manager_t* socket_manager = handle->data;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  ct_connection_mark_as_closed(socket_state->connection);
  if (socket_state->connection->connection_callbacks.connection_error) {
    log_debug("Invoking connection connection error callback due to abort");
    socket_state->connection->connection_callbacks.connection_error(socket_state->connection);
  }
  else {
    log_debug("Connection error callback not set, on abort");
  }
}

void on_stop_listen(uv_handle_t* handle) {
  // TODO - invoke stopped callback for listener
  (void)handle;
}

void on_libuv_close(uv_handle_t* handle) {
  log_debug("libuv TCP handle successfully closed");
  ct_socket_manager_t* socket_manager = handle->data;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  socket_state->close_cb(socket_state->connection);
}

void tcp_on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  ct_socket_manager_t* socket_manager = handle->data;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  ct_connection_t* connection = socket_state->connection;
  if (nread == UV_EOF) {
    log_info("TCP connection closed by peer");
    ct_connection_close(connection);
    free(buf->base);
    return;
  }
  if (nread < 0) {
    log_error("Read error for TCP connection: %s\n", uv_strerror(nread));
    if (!uv_is_closing((uv_handle_t*)socket_state->tcp_handle)) {
      log_debug("Closing tcp handle: %p", (void*)socket_state->tcp_handle);
      uv_close((uv_handle_t*)socket_state->tcp_handle, on_abort);
    }

    // Quote from libuv docs:
    // When nread < 0, the buf parameter might not point to a valid buffer; in that case buf.len and buf.base are both set to 0
    if (buf->len || buf->base) {
      free(buf->base);
    }
    return;
  }
  log_trace("Read %zd bytes from TCP connection", nread);

  // Delegate to connection receive handler (handles framing if present)
  ct_connection_on_protocol_receive(connection, buf->base, nread);
  free(buf->base);
}

void on_clone_connect(struct uv_connect_s *req, int status) {
  ct_socket_manager_t* socket_manager = req->handle->data;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  ct_connection_t* connection = socket_state->connection;

  if (status < 0) {
    log_error("Cloned TCP connection failed: %s", uv_strerror(status));
    ct_connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection);
    }
    return;
  }

  log_info("Cloned TCP connection established successfully");

  int rc = uv_read_start((uv_stream_t*)socket_state->tcp_handle,
                         alloc_cb, tcp_on_read);
  if (rc < 0) {
    log_error("Failed to start reading on cloned connection: %s", uv_strerror(rc));
    ct_connection_close(connection);
    return;
  }
  ct_connection_mark_as_established(connection);

  // Call ready callback to notify that cloned connection is established
  if (connection->connection_callbacks.ready) {
    connection->connection_callbacks.ready(connection);
  }
}

void on_connect(struct uv_connect_s *req, int status) {
  ct_socket_manager_t* socket_manager = req->handle->data;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  ct_connection_t* connection = socket_state->connection;
  if (status < 0) {
    log_error("ct_connection_t error: %s", uv_strerror(status));
    ct_connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection);
    }
    return;
  }
  log_info("Successfully connected to remote endpoint using TCP");
  uv_read_start((uv_stream_t*)socket_state->tcp_handle, alloc_cb, tcp_on_read);
  ct_connection_mark_as_established(connection);
  if (socket_state->initial_message) {
    tcp_send(connection, socket_state->initial_message, socket_state->initial_message_context);
  }

  if (connection->connection_callbacks.ready) {
    connection->connection_callbacks.ready(connection);
  }
}

void on_write(uv_write_t* req, int status) {
  ct_socket_manager_t* socket_manager = req->handle->data;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  ct_connection_t* connection = socket_state->connection;
  // ct_message_t* message = (ct_message_t*)req->data;

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
  // if (message) {
  //   ct_message_free(message);
  // }
  // log_debug("Freeing write request");
  // free(req);
}

int tcp_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context) {
  (void)connection_callbacks;
  log_info("Initiating TCP connection");
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  int rc = uv_tcp_init(event_loop, new_tcp_handle);

  if (rc < 0) {
    log_error("Error initializing tcp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  uv_connect_t* connect_req = malloc(sizeof(uv_connect_t));
  connection->socket_manager->internal_socket_manager_state = ct_tcp_socket_state_new(connection,
                                                              NULL,
                                                              initial_message,
                                                              initial_message_context,
                                                              connect_req,
                                                              new_tcp_handle);
  if (!connection->socket_manager->internal_socket_manager_state) {
    log_error("Failed to allocate memory for TCP connection state");
      log_debug("Closing tcp handle: %p", (void*)new_tcp_handle);
    uv_close((uv_handle_t*)new_tcp_handle, on_libuv_close);
    return -ENOMEM;
  }

  new_tcp_handle->data = ct_socket_manager_ref(connection->socket_manager);

  uint32_t keepalive_timeout = connection->transport_properties->connection_properties.list[KEEP_ALIVE_TIMEOUT].value.uint32_val;
  if (keepalive_timeout != CONN_TIMEOUT_DISABLED) {
    log_info("Setting TCP keepalive with timeout: %u seconds", keepalive_timeout);
    rc = uv_tcp_keepalive(new_tcp_handle, true, keepalive_timeout);
    if (rc < 0) {
      log_warn("Error setting TCP keepalive: %s", uv_strerror(rc));
    }
  }

  rc = uv_tcp_connect(connect_req,
                 new_tcp_handle,
                 (const struct sockaddr*)remote_endpoint_get_resolved_address(ct_connection_get_remote_endpoint(connection)),
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
  rc = resolve_local_endpoint_from_handle((uv_handle_t*)new_tcp_handle, connection);
  if (rc < 0) {
    log_error("Failed to get TCP socket name: %s", uv_strerror(rc));
    ct_connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection);
    }
    return rc;
  }

  return 0;
}

int tcp_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks) {
  (void)connection_callbacks;
  log_info("Initiating TCP connection");
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  int rc = uv_tcp_init(event_loop, new_tcp_handle);

  if (rc < 0) {
    log_error("Error initializing tcp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  uv_connect_t* connect_req = malloc(sizeof(uv_connect_t));
  new_tcp_handle->data = ct_socket_manager_ref(connection->socket_manager);
  connection->socket_manager->internal_socket_manager_state = ct_tcp_socket_state_new(
    connection,
    NULL,
    NULL,
    NULL,
    connect_req,
    new_tcp_handle
  );
  if (!connection->socket_manager->internal_socket_manager_state) {
    log_error("Failed to allocate memory for TCP connection state");
      log_debug("Closing tcp handle: %p", (void*)new_tcp_handle);
    uv_close((uv_handle_t*)new_tcp_handle, on_libuv_close);
    return -ENOMEM;
  }

  log_debug("Initiated with new TCP handle: %p", new_tcp_handle);

  uint32_t keepalive_timeout = connection->transport_properties->connection_properties.list[KEEP_ALIVE_TIMEOUT].value.uint32_val;
  if (keepalive_timeout != CONN_TIMEOUT_DISABLED) {
    log_info("Setting TCP keepalive with timeout: %u seconds", keepalive_timeout);
    rc = uv_tcp_keepalive(new_tcp_handle, true, keepalive_timeout);
    if (rc < 0) {
      log_warn("Error setting TCP keepalive: %s", uv_strerror(rc));
    }
  }

  rc = uv_tcp_connect(connect_req,
                 new_tcp_handle,
                 (const struct sockaddr*)remote_endpoint_get_resolved_address(ct_connection_get_remote_endpoint(connection)),
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
  rc = resolve_local_endpoint_from_handle((uv_handle_t*)new_tcp_handle, connection);
  if (rc < 0) {
    log_error("Failed to get TCP socket name: %s", uv_strerror(rc));
    ct_connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection);
    }
    return rc;
  }

  return 0;
}

int tcp_close(ct_connection_t* connection, ct_on_connection_close_cb on_connection_close) {
  log_info("Closing TCP connection: %s", connection->uuid);
  (void)on_connection_close;

  ct_tcp_socket_state_t* socket_state = connection->socket_manager->internal_socket_manager_state;
  socket_state->close_cb = on_connection_close;
      log_debug("Closing tcp handle: %p", (void*)socket_state->tcp_handle);
  uv_close((uv_handle_t*)socket_state->tcp_handle, on_libuv_close);

  return 0;
}

void tcp_abort(ct_connection_t* connection) {
  log_info("Aborting TCP connection: %s", connection->uuid);
  ct_tcp_socket_state_t* socket_state = connection->socket_manager->internal_socket_manager_state;

  // TCP connections are always STANDALONE - abort with RST flag
  uv_tcp_close_reset(socket_state->tcp_handle, on_abort);
  ct_connection_mark_as_closed(connection);
}

int tcp_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* ctx) {
  (void)ctx;
  log_debug("Sending message over TCP");

  uv_buf_t buffer = uv_buf_init(message->content, message->length);

  uv_write_t *req = malloc(sizeof(uv_write_t));
  if (!req) {
    log_error("Failed to allocate memory for write request");
    return -errno;
  }

  // Attach message to request so it can be freed in the callback
  req->data = message;

  ct_socket_manager_t* socket_manager = connection->socket_manager;
  ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  uv_tcp_t* tcp_handle = socket_state->tcp_handle;

  int rc = uv_write(req, (uv_stream_t*)tcp_handle, &buffer, 1, on_write);
  if (rc != 0) {
    log_error("Error sending message over TCP: %s", uv_strerror(rc));
    free(req);
    return rc;
  }
  return 0;
}

int tcp_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Listening via TCP");
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  ct_listener_t* listener = socket_manager->listener;

  int rc = uv_tcp_init(event_loop, new_tcp_handle);
  if (rc < 0) {
    log_error( "Error initializing tcp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }


  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(listener);
  rc = uv_tcp_bind(new_tcp_handle, (const struct sockaddr*)local_endpoint_get_resolved_address(&local_endpoint), 0);
  if (rc < 0) {
    log_error("Error binding TCP handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  log_debug("Listening on handle: %p", new_tcp_handle);
  rc = uv_listen((uv_stream_t*)new_tcp_handle, SOMAXCONN, new_stream_connection_cb);

  if (rc < 0) {
    log_error("Error starting TCP listen: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  socket_manager->internal_socket_manager_state = ct_tcp_socket_state_new(NULL, listener, NULL, NULL, NULL, new_tcp_handle);
  new_tcp_handle->data = ct_socket_manager_ref(socket_manager);

  return 0;
}

void new_stream_connection_cb(uv_stream_t *server, int status) {
  log_debug("New TCP connection received for ct_listener_t");
  if (status < 0) {
    log_error("New connection error: %s", uv_strerror(status));
    return;
  }
  uv_tcp_t* server_conn_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (!server_conn_tcp_handle) {
    log_error("Failed to allocate memory for new TCP client");
    return;
  }
  int rc = uv_tcp_init(event_loop, server_conn_tcp_handle);
  if (rc < 0) {
    log_error("Error initializing TCP client handle: %s", uv_strerror(rc));
    free(server_conn_tcp_handle);
    return;
  }

  ct_socket_manager_t* listener_socket_manager = server->data;
  ct_listener_t* listener = listener_socket_manager->listener;

  rc = uv_accept(server, (uv_stream_t*)server_conn_tcp_handle);
  if (rc < 0) {
    log_error("Error accepting new TCP connection: %s", uv_strerror(rc));
      log_debug("Closing tcp handle: %p", (void*)server_conn_tcp_handle);
    uv_close((uv_handle_t*)server_conn_tcp_handle, on_libuv_close);
    return;
  }

  ct_socket_manager_t* server_conn_socket_manager = ct_socket_manager_new(&tcp_protocol_interface, listener);

  struct sockaddr_storage addr;
  int namelen = sizeof(addr);
  rc = uv_tcp_getpeername(server_conn_tcp_handle, (struct sockaddr*)&addr, &namelen);
  ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
  ct_remote_endpoint_from_sockaddr(remote_endpoint, &addr);

  ct_connection_t* server_conn = ct_connection_create_server_connection(
      server_conn_socket_manager,
      remote_endpoint,
      listener->security_parameters,
      NULL
  );

  if (!server_conn) {
    log_error("Failed to build connection from received handle");
      log_debug("Closing tcp handle: %p", (void*)server_conn_tcp_handle);
    uv_close((uv_handle_t*)server_conn_tcp_handle, on_libuv_close);
    return;
  }

  server_conn_socket_manager->internal_socket_manager_state = ct_tcp_socket_state_new(server_conn, listener, NULL, NULL, NULL, server_conn_tcp_handle);
  server_conn_tcp_handle->data = ct_socket_manager_ref(server_conn_socket_manager);
  log_debug("Server conn socket tcp handle: %p", server_conn_tcp_handle);

  rc = uv_read_start((uv_stream_t*)server_conn_tcp_handle, alloc_cb, tcp_on_read);
  if (rc < 0) {
    log_error("Could not start reading from TCP connection: %s", uv_strerror(rc));
      log_debug("Closing tcp handle: %p", (void*)server_conn_tcp_handle);
    uv_close((uv_handle_t*)server_conn_tcp_handle, on_libuv_close);
    ct_connection_close(server_conn);
    free(server_conn);
    return;
  }

  rc = resolve_local_endpoint_from_handle((uv_handle_t*)server_conn_tcp_handle, server_conn);
  if (rc < 0) {
    log_error("Failed to get TCP socket name: %s", uv_strerror(rc));
  }

  log_trace("TCP invoking new connection callback");
  ct_connection_mark_as_established(server_conn);
  if (listener->listener_callbacks.connection_received) {
    log_debug("Invoking connection received callback for listener");
    log_debug("Listener: %p, server_conn: %p", listener, server_conn);
    listener->listener_callbacks.connection_received(listener, server_conn);
  }
  else {
    log_debug("No connection received callback registered for listener");
  }
}

int tcp_stop_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Stopping TCP listen for ct_socket_manager_t %p", (void*)socket_manager);

  if (socket_manager->internal_socket_manager_state) {
    ct_tcp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
    log_debug("Closing tcp handle: %p", (void*)socket_state->tcp_handle);
    uv_close((uv_handle_t*)socket_state->tcp_handle, on_stop_listen);
  }
  return 0;
}

int tcp_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer) {
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  int rc = uv_tcp_getpeername((uv_tcp_t*)peer, (struct sockaddr *)&remote_addr, &addr_len);
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

int tcp_clone_connection(const struct ct_connection_s* source_connection,
                         struct ct_connection_s* target_connection) {
  if (!source_connection || !target_connection) {
    log_error("Source or target connection is NULL in tcp_clone_connection");
    return -EINVAL;
  }

  log_info("Cloning TCP connection");

  // Allocate and initialize TCP handle
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  int rc = uv_tcp_init(event_loop, new_tcp_handle);
  if (rc < 0) {
    log_error("Error initializing tcp handle for clone: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }

  if (target_connection->socket_manager) {
    ct_socket_manager_unref(target_connection->socket_manager);
  }

  ct_socket_manager_t* socket_manager = ct_socket_manager_new(&tcp_protocol_interface, NULL);
  target_connection->socket_manager = ct_socket_manager_ref(socket_manager);

  uv_connect_t* connect_req = malloc(sizeof(uv_connect_t));

  socket_manager->internal_socket_manager_state = ct_tcp_socket_state_new(target_connection, NULL, NULL, NULL, connect_req, new_tcp_handle);

  // Copy TCP keepalive settings
  uint32_t keepalive_timeout = target_connection->transport_properties
      ->connection_properties.list[KEEP_ALIVE_TIMEOUT].value.uint32_val;

  if (keepalive_timeout != CONN_TIMEOUT_DISABLED) {
    log_info("Setting TCP keepalive with timeout: %u seconds", keepalive_timeout);
    rc = uv_tcp_keepalive(new_tcp_handle, true, keepalive_timeout);
    if (rc < 0) {
      log_warn("Error setting TCP keepalive: %s", uv_strerror(rc));
    }
  }

  if (!connect_req) {
    log_error("Failed to allocate connect request");
    log_debug("Closing tcp handle: %p", (void*)new_tcp_handle);
    uv_close((uv_handle_t*)new_tcp_handle, on_libuv_close);
    return -ENOMEM;
  }

  rc = uv_tcp_connect(
      connect_req,
      new_tcp_handle,
      (const struct sockaddr*)remote_endpoint_get_resolved_address(ct_connection_get_remote_endpoint(target_connection)),
      on_clone_connect
  );

  if (rc < 0) {
    log_error("Error initiating TCP clone connection: %s", uv_strerror(rc));
    free(connect_req);
    log_debug("Closing tcp handle: %p", (void*)new_tcp_handle);
    uv_close((uv_handle_t*)new_tcp_handle, on_libuv_close);
    return rc;
  }

  log_info("TCP clone connection initiated, establishing asynchronously");
  return 0;
}

int tcp_free_state(ct_connection_t* connection) {
  return 0; // Fix after ownership refactor
  log_trace("Freeing TCP connection resources");
  if (!connection || !connection->internal_connection_state) {
    log_warn("TCP connection or internal state is NULL during free_state");
    log_debug("Connection pointer: %p", (void*)connection);
    if (connection) {
      log_debug("Internal connection state pointer: %p", (void*)connection->internal_connection_state);
    }
    return -EINVAL;
  }
  ct_tcp_socket_state_t* socket_state = ct_connection_get_socket_state(connection);

  tcp_connection_state_free(socket_state);
  connection->internal_connection_state = NULL;

  return 0;
}

int tcp_free_connection_group_state(ct_connection_group_t* connection_group) {
  (void)connection_group;
  return 0;
}

int tcp_close_socket(ct_socket_manager_t* socket_manager) {
  (void)socket_manager;
  // TODO implement
  return 0;
}
