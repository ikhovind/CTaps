#include "connection.h"

#include <connections/listener/listener.h>
#include <logging/log.h>
#include <string.h>

#include "connections/listener/socket_manager/socket_manager.h"
#include "endpoints/remote/remote_endpoint.h"
#include "glib.h"
#include "message/message.h"
#include "message/message_context/message_context.h"
#include "protocols/protocol_interface.h"

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

void connection_build_from_listener(Connection* connection, const Listener* listener, const RemoteEndpoint* remote_endpoint) {
  memset(connection, 0, sizeof(Connection));
  connection->local_endpoint = listener->local_endpoint;
  connection->transport_properties = listener->transport_properties;
  connection->remote_endpoint = *remote_endpoint;
  connection->protocol_uv_handle = listener->socket_manager->protocol_uv_handle;
  connection->protocol = listener->socket_manager->protocol_impl;
  connection->socket_manager = listener->socket_manager;
  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->open_type = CONNECTION_OPEN_TYPE_PASSIVE;
}

void connection_close(Connection* connection) {
  if (connection->open_type == CONNECTION_OPEN_TYPE_PASSIVE) {
    log_info("Closing Connection relying on socket manager, removing from socket manager\n");
    socket_manager_remove_connection(connection->socket_manager, connection);
  }
  else {
    log_info("Closing independent connection");
    connection->protocol.close(connection);
  }
}
