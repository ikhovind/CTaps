
#include <logging/log.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#include "connection/socket_manager/socket_manager.h"
#include "message/message.h"
#include "ctaps.h"
#include "glib.h"

// Passed as callbacks to framer implementations
void ct_connection_send_to_protocol(ct_connection_t* connection,
                                   ct_message_t* message,
                                   ct_message_context_t* context);

void ct_connection_deliver_to_app(ct_connection_t* connection,
                                 ct_message_t* message,
                                 ct_message_context_t* context);


int ct_send_message(ct_connection_t* connection, ct_message_t* message) {
  return ct_send_message_full(connection, message, NULL);
}

int ct_send_message_full(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context) {
  // Deep copy the message so the library owns its lifetime
  // This allows framers to be asynchronous without worrying about message validity
  ct_message_t* message_copy = ct_message_deep_copy(message);
  if (!message_copy) {
    log_error("Failed to deep copy message");
    return -ENOMEM;
  }

  if (connection->framer_impl != NULL) {
    log_info("User sending message on connection with framer");
    connection->framer_impl->encode_message(connection, message_copy, message_context, ct_connection_send_to_protocol);
    return 0;
  }
  log_info("User sending message on connection without framer");
  ct_connection_send_to_protocol(connection, message_copy, message_context);
  return 0;
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
  if (!ptr) {
    log_error("Failed to allocate memory for receive callbacks");
    return -ENOMEM;
  }
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
  connection->framer_impl = NULL;  // No framer by default
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

ct_connection_t* ct_connection_build_from_received_handle(const struct ct_listener_s* listener, uv_stream_t* received_handle) {
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
  connection->framer_impl = NULL;  // No framer by default
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

void ct_connection_send_to_protocol(ct_connection_t* connection,
                                   ct_message_t* message,
                                   ct_message_context_t* context) {
  int rc = connection->protocol.send(connection, message, context);
  if (rc < 0) {
    log_error("Error sending message to protocol: %d", rc);
  }
}

void ct_connection_deliver_to_app(ct_connection_t* connection,
                                 ct_message_t* message,
                                 ct_message_context_t* context) {
  // Check if there's a waiting receive callback
  if (g_queue_is_empty(connection->received_callbacks)) {
    log_debug("No receive callback ready, queueing message");
    g_queue_push_tail(connection->received_messages, message);
  } else {
    log_debug("Receive callback ready, calling it");
    ct_receive_callbacks_t* receive_callback = g_queue_pop_head(connection->received_callbacks);

    ct_message_context_t ctx = context ? *context : (ct_message_context_t){0};
    ctx.user_receive_context = receive_callback->user_receive_context;

    receive_callback->receive_callback(connection, &message, &ctx);
    free(receive_callback);
  }
}

void ct_connection_on_protocol_receive(ct_connection_t* connection,
                                       const void* data,
                                       size_t len) {
  if (connection->framer_impl != NULL) {
    // Framer present - let it decode, it will call ct_connection_deliver_to_app()
    connection->framer_impl->decode_data(connection, data, len, ct_connection_deliver_to_app);
  } else {
    // No framer - deliver directly to application
    ct_message_t* received_message = malloc(sizeof(ct_message_t));
    if (!received_message) {
      log_error("Failed to allocate memory for received message");
      return;
    }
    received_message->content = malloc(len);
    if (!received_message->content) {
      log_error("Failed to allocate memory for received message content");
      free(received_message);
      return;
    }
    received_message->length = len;
    memcpy(received_message->content, data, len);

    ct_connection_deliver_to_app(connection, received_message, NULL);
  }
}
