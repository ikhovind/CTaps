#include "connection.h"

#include <connections/listener/listener.h>

#include "message/message.h"
#include "protocols/protocol_interface.h"

int send_message(Connection* connection, Message* message) {
  printf("Sending message to port %d\n", connection->remote_endpoint.port);
  return connection->protocol.send(connection, message);
}

int receive_message(Connection* connection,
                    ReceiveMessageRequest receive_message_cb
                    ) {
  printf("Trying to receive message for connection to port %d\n", connection->remote_endpoint.port);
  return connection->protocol.receive(connection, receive_message_cb);
}

void connection_build_from_listener(Connection* connection, Listener* listener, RemoteEndpoint* remote_endpoint) {
  memset(connection, 0, sizeof(Connection));
  connection->local_endpoint = listener->local_endpoint;
  connection->transport_properties = listener->transport_properties;
  connection->remote_endpoint = *remote_endpoint;
  connection->protocol = listener->socket_manager->protocol_impl;
  connection->socket_manager = listener->socket_manager;
  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->open_type = CONNECTION_OPEN_TYPE_PASSIVE;
}

void connection_close(Connection* connection) {
  if (connection->open_type == CONNECTION_OPEN_TYPE_PASSIVE) {
    printf("Connection relying on socket manager, removing from socket manager\n");
    socket_manager_remove_connection(connection->socket_manager, connection);
  }
  else {
    printf("Connection independent of socket manager\n");
    connection->protocol.close(connection);
  }
}
