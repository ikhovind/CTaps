#include "tcp.h"
#include "connections/connection/connection.h"
#include "connections/listener/listener.h"
#include "connections/listener/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
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
  log_info("TCP received message for Connection: %p", connection);
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
  uv_read_start((uv_stream_t*)connection->protocol_state, alloc_cb, tcp_on_read);
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
  log_info("Initiating TCP connection");
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  connection->protocol_state = (uv_handle_t*)new_tcp_handle;

  rc = uv_tcp_init(ctaps_event_loop, new_tcp_handle);

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
    connection_close(connection);
    if (connection->connection_callbacks.establishment_error) {
      connection->connection_callbacks.establishment_error(connection, connection->connection_callbacks.user_data);
    }
    return rc;
  }
  return 0;
}

int tcp_close(const Connection* connection) {
  log_info("Closing TCP connection");

  if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
    log_info("Closing multiplexed TCP connection, removing from socket manager");
    int rc = socket_manager_remove_connection(connection->socket_manager, (Connection*)connection);
    if (rc < 0) {
      log_error("Error removing TCP connection from socket manager: %d", rc);
      return rc;
    }
  } else {
    // Standalone connection - close the TCP handle
    if (connection->protocol_state) {
      uv_close(connection->protocol_state, on_close);
    }
  }

  ((Connection*)connection)->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;

  return 0;
}

int tcp_send(Connection* connection, Message* message, MessageContext* ctx) {
  log_debug("Sending message over TCP");
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
  int rc = uv_write(req, (uv_tcp_t*)connection->protocol_state, buffer, 1, on_write);
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

int tcp_listen(SocketManager* socket_manager) {
  log_debug("Listening via TCP");
  int rc;
  uv_tcp_t* new_tcp_handle = malloc(sizeof(uv_tcp_t));
  if (new_tcp_handle == NULL) {
    log_error("Failed to allocate memory for TCP handle");
    return -ENOMEM;
  }

  Listener* listener = socket_manager->listener;

  rc = uv_tcp_init(ctaps_event_loop, new_tcp_handle);
  if (rc < 0) {
    log_error( "Error initializing tcp handle: %s", uv_strerror(rc));
    free(new_tcp_handle);
    return rc;
  }


  LocalEndpoint local_endpoint = listener_get_local_endpoint(listener);
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
  socket_manager->protocol_state = (uv_handle_t*)new_tcp_handle;
  new_tcp_handle->data = listener;

  return 0;
}

void new_stream_connection_cb(uv_stream_t *server, int status) {
  log_debug("New TCP connection received for Listener");
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
  rc = uv_tcp_init(ctaps_event_loop, client);
  if (rc < 0) {
    log_error("Error initializing TCP client handle: %s", uv_strerror(rc));
    free(client);
    return;
  }

  Listener* listener = server->data;

  rc = uv_accept(server, (uv_stream_t*)client);
  if (rc < 0) {
    log_error("Error accepting new TCP connection: %s", uv_strerror(rc));
    uv_close((uv_handle_t*)client, on_close);
    return;
  }

  Connection* connection = connection_build_from_received_handle(listener, (uv_stream_t*)client);

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
    connection_close(connection);
    free(connection);
    return;
  }

  log_trace("TCP invoking new connection callback");
  listener->listener_callbacks.connection_received(listener, connection, listener->listener_callbacks.user_data);
}

int tcp_stop_listen(SocketManager* socket_manager) {
  log_debug("Stopping TCP listen for SocketManager %p", (void*)socket_manager);

  if (socket_manager->protocol_state) {
    uv_close(socket_manager->protocol_state, on_close);
    socket_manager->protocol_state = NULL;
  }
  return 0;
}

int tcp_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer) {
  int rc;
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  rc = uv_tcp_getpeername((uv_tcp_t*)peer, (struct sockaddr *)&remote_addr, &addr_len);
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

void tcp_retarget_protocol_connection(Connection* from_connection, Connection* to_connection) {
  // For TCP, protocol_state is the uv_tcp_t handle directly
  // Update the handle's data pointer to reference the new connection
  if (from_connection->protocol_state) {
    uv_handle_t* handle = (uv_handle_t*)from_connection->protocol_state;
    handle->data = to_connection;
  }
}
