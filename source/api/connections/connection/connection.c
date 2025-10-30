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
  connection->protocol_state = listener->socket_manager->protocol_state;
  connection->protocol = listener->socket_manager->protocol_impl;
  connection->socket_manager = listener->socket_manager;
  connection->security_parameters = listener->security_parameters;
  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->open_type = CONNECTION_OPEN_TYPE_MULTIPLEXED;
}

void connection_close(Connection* connection) {
  int rc;
  if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
    log_info("Closing Connection relying on socket manager, removing from socket manager\n");
    rc = socket_manager_remove_connection(connection->socket_manager, connection);
    if (rc < 0) {
      log_error("Error removing connection from socket manager: %d", rc);
    }
  }
  else {
    log_info("Closing standalone connection");
    rc = connection->protocol.close(connection);
    if (rc < 0) {
      log_error("Error closing connection: %d", rc);
    }
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
  rc = listener->socket_manager->protocol_impl.remote_endpoint_from_peer((uv_handle_t*)received_handle, &connection->remote_endpoint);
  if (rc < 0) {
    log_error("Could not build remote endpoint from received handle's remote address");
    free(connection);
    return NULL;
  }

  connection->protocol = listener->socket_manager->protocol_impl;
  connection->open_type = CONNECTION_TYPE_STANDALONE;
  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->protocol_state = (uv_handle_t*)received_handle;

  return connection;
}
