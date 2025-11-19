
#include <logging/log.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#include "ctaps.h"
#include "connection/socket_manager/socket_manager.h"
#include "glib.h"

int ct_send_message(ct_connection_t* connection, ct_message_t* message) {
  return connection->protocol.send(connection, message, NULL);
}

int ct_send_message_full(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context) {
  return connection->protocol.send(connection, message, message_context);
}

int ct_receive_message(ct_connection_t* connection,
                    ct_receive_callbacks_t receive_callbacks
                    ) {
  log_info("User attempting to receive message on connection: %p", connection);
  if (!connection->received_messages || !connection->received_callbacks) {
    log_error("ct_connection_t queues not initialized for receiving messages");
    return -EIO;
  }

  if (!g_queue_is_empty(connection->received_messages)) {
    log_debug("Calling receive callback immediately");
    ct_message_t* received_message = g_queue_pop_head(connection->received_messages);
    ct_message_context_t ctx = {0};
    ctx.user_receive_context = receive_callbacks.user_receive_context;
    receive_callbacks.receive_callback(connection, &received_message, &ctx);
    return 0;
  }

  ct_receive_callbacks_t* ptr = malloc(sizeof(ct_receive_callbacks_t));
  memcpy(ptr, &receive_callbacks, sizeof(ct_receive_callbacks_t));

  // If we don't have a message to receive, add the callback to the queue of
  // waiting callbacks
  log_debug("No message ready, pushing receive callback to queue %p", (void*)connection->received_callbacks);
  g_queue_push_tail(connection->received_callbacks, ptr);
  return 0;
}

void ct_connection_build_multiplexed(ct_connection_t* connection, const ct_listener_t* listener, const ct_remote_endpoint_t* remote_endpoint) {
  memset(connection, 0, sizeof(ct_connection_t));
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

void ct_connection_close(ct_connection_t* connection) {
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

ct_connection_t* ct_connection_build_from_received_handle(const struct ct_listener_t* listener, uv_stream_t* received_handle) {
  log_debug("Building ct_connection_t from received handle");
  int rc;
  ct_connection_t* connection = malloc(sizeof(ct_connection_t));
  if (!connection) {
    log_error("Failed to allocate memory for ct_connection_t");
    return NULL;
  }
  memset(connection, 0, sizeof(ct_connection_t));

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

void ct_connection_free(ct_connection_t* connection) {
  if (connection->received_callbacks) {
    g_queue_free(connection->received_callbacks);
  }
  if (connection->received_messages) {
    while (!g_queue_is_empty(connection->received_messages)) {
      ct_message_t* msg = g_queue_pop_head(connection->received_messages);
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
