#include "tcp.h"
#include "ctaps.h"
#include <errno.h>
#include <logging/log.h>
#include <stdbool.h>
#include <uv.h>

void on_close(uv_handle_t* handle) {
  free(handle);
}

void on_connect(struct uv_connect_s *req, int status) {
  if (status < 0) {
    log_error("Connection error: %s", uv_strerror(status));
    Connection* connection = (Connection*)req->handle->data;
    connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;
    if (connection->connection_callbacks.connection_error) {
      connection->connection_callbacks.connection_error(connection, connection->connection_callbacks.user_data);
    }
    uv_close((uv_handle_t*)req->handle, on_close);
    free(req);
    return;
  }
  log_info("Successfully connected to remote endpoint using TCP");
  Connection* connection = (Connection*)req->handle->data;
  if (connection->connection_callbacks.ready) {
    connection->connection_callbacks.ready(connection, connection->connection_callbacks.user_data);
  }
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
  uv_tcp_connect(connect_req,
                 new_tcp_handle,
                 (const struct sockaddr*)&connection->remote_endpoint.data.resolved_address,
                 on_connect);
  
  return 0;
}

int tcp_close(const Connection* connection) {
  return -ENOSYS;
}
int tcp_send(Connection* connection, Message* message, MessageContext*) {
  return -ENOSYS;
}
int tcp_receive(Connection* connection, ReceiveCallbacks receive_callbacks) {
  return -ENOSYS;
}
int tcp_listen(struct SocketManager* socket_manager) {
  return -ENOSYS;
}

int tcp_stop_listen(struct SocketManager* listener) {
  return -ENOSYS;
}
