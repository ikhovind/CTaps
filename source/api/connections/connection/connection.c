#include "connection.h"

#include <connections/listener/listener.h>
#include <logging/log.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#include "connections/listener/socket_manager/socket_manager.h"
#include "endpoints/remote/remote_endpoint.h"
#include "glib.h"
#include "message/message.h"
#include "message/message_context/message_context.h"
#include "protocols/protocol_interface.h"
#include "transport_properties/connection_properties/connection_properties.h"

int send_message(Connection* connection, Message* message) {
  return connection->protocol.send(connection, message, NULL);
}

int send_message_full(Connection* connection, Message* message, MessageContext* message_context) {
  return connection->protocol.send(connection, message, message_context);
}

int receive_message(Connection* connection,
                    ReceiveCallbacks receive_callbacks
                    ) {
  return connection->protocol.receive(connection, receive_callbacks);
}

void connection_build_multiplexed(Connection* connection, const Listener* listener, const RemoteEndpoint* remote_endpoint) {
  memset(connection, 0, sizeof(Connection));
  connection->local_endpoint = listener->local_endpoint;
  connection->transport_properties = listener->transport_properties;
  connection->remote_endpoint = *remote_endpoint;
  connection->protocol_uv_handle = listener->socket_manager->protocol_uv_handle;
  connection->protocol = listener->socket_manager->protocol_impl;
  connection->socket_manager = listener->socket_manager;
  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->open_type = CONNECTION_OPEN_TYPE_MULTIPLEXED;
}

void connection_close(Connection* connection) {
  if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
    log_info("Closing Connection relying on socket manager, removing from socket manager\n");
    socket_manager_remove_connection(connection->socket_manager, connection);
  }
  else {
    log_info("Closing standalone connection");
    connection->protocol.close(connection);
    connection->transport_properties.connection_properties.list[STATE].value.uint32_val = CONN_STATE_CLOSED;
  }
}

Connection* connection_build_from_received_handle(const struct Listener* listener, uv_stream_t* received_handle) {
  log_debug("Building Connection from received handle");
  int rc;
  Connection* connection = malloc(sizeof(Connection));
  if (!connection) {
    log_error("Failed to allocate memory for Connection");
    return NULL;
  }
  memset(connection, 0, sizeof(Connection));

  connection->transport_properties = listener->transport_properties;
  connection->local_endpoint = listener->local_endpoint;
  struct sockaddr_storage remote_addr;
  int addr_len = sizeof(remote_addr);
  // TODO - this is TCP specific for now
  rc = uv_tcp_getpeername((uv_tcp_t*)received_handle, (struct sockaddr *)&remote_addr, &addr_len);
  if (rc < 0) {
    log_error("Could not get remote address from received handle: %s", uv_strerror(rc));
    free(connection);
    return NULL;
  }
  rc = remote_endpoint_from_sockaddr(&connection->remote_endpoint, &remote_addr);
  if (rc < 0) {
    log_error("Could not build remote endpoint from received handle's remote address");
    free(connection);
    return NULL;
  }

  connection->protocol = listener->socket_manager->protocol_impl;
  connection->open_type = CONNECTION_TYPE_STANDALONE;
  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->protocol_uv_handle = (uv_handle_t*)received_handle;

  return connection;
}
