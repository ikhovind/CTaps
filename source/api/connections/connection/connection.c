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
  log_info("User attempting to receive message on connection: %p", connection);
  if (!connection->received_messages || !connection->received_callbacks) {
    log_error("Connection queues not initialized for receiving messages");
  }

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
  log_debug("No message ready, pushing receive callback to queue %p", (void*)connection->received_callbacks);
  g_queue_push_tail(connection->received_callbacks, ptr);
  return 0;
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
  log_info("Closing connection: %p", (void*)connection);

  // Always let the protocol handle the close logic
  // For protocols like QUIC, this will initiate close handshake
  // The protocol is responsible for cleaning up (removing from socket manager, etc.)
  rc = connection->protocol.close(connection);
  if (rc < 0) {
    log_error("Error closing connection: %d", rc);
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

void connection_free(Connection* connection) {
  if (connection->received_callbacks) {
    g_queue_free(connection->received_callbacks);
  }
  if (connection->received_messages) {
    while (!g_queue_is_empty(connection->received_messages)) {
      Message* msg = g_queue_pop_head(connection->received_messages);
      if (msg) {
        if (msg->content) {
          free(msg->content);
        }
        free(msg);
      }
    }
    g_queue_free(connection->received_messages);
  }
  free(connection);
}
