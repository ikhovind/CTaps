
#include <logging/log.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#include "connection/socket_manager/socket_manager.h"
#include "connection/connection.h"
#include "connection/connection_group.h"
#include "util/uuid_util.h"
#include "message/message.h"
#include "ctaps.h"
#include "glib.h"

void ct_connection_build_with_connection_group(ct_connection_t* connection) {
  memset(connection, 0, sizeof(ct_connection_t));
  generate_uuid_string(connection->uuid);

  // Create connection group for this connection
  ct_connection_group_t* group = malloc(sizeof(ct_connection_group_t));
  if (!group) {
    log_error("Failed to allocate connection group");
    return;
  }

  // Generate UUID for the connection group
  generate_uuid_string(group->connection_group_id);

  // Initialize connections hash table (keyed by connection UUID)
  group->connections = g_hash_table_new(g_str_hash, g_str_equal);
  group->connection_group_state = NULL; // Will be set by protocol implementation
  group->num_active_connections = 0;

  // Add this connection to the group
  g_hash_table_insert(group->connections, connection->uuid, connection);
  group->num_active_connections++;

  connection->connection_group = group;
}

ct_connection_t* create_empty_connection_with_uuid() {
  ct_connection_t* connection = malloc(sizeof(ct_connection_t));
  if (!connection) {
    log_error("Failed to allocate memory for ct_connection_t");
    return NULL;
  }
  memset(connection, 0, sizeof(ct_connection_t));
  generate_uuid_string(connection->uuid);

  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  
  return connection;
}

// Passed as callbacks to framer implementations
int ct_connection_send_to_protocol(ct_connection_t* connection,
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
  // Ownership is transferred to the framer or protocol send function, so it is freed in protocol implementation
  ct_message_t* message_copy = ct_message_deep_copy(message);
  if (!message_copy) {
    log_error("Failed to deep copy message");
    return -ENOMEM;
  }
  log_info("Message copy size: %zu", message_copy->length);

  int rc;
  if (connection->framer_impl != NULL) {
    log_info("User sending message on connection with framer");
    rc = connection->framer_impl->encode_message(connection, message_copy, message_context, ct_connection_send_to_protocol);
    if (rc < 0) {
      log_error("Framer encode_message failed: %d", rc);
    }
  } else {
    log_info("User sending message on connection without framer");
    rc = ct_connection_send_to_protocol(connection, message_copy, message_context);
  }

  return rc;
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
  ct_connection_build_with_connection_group(connection);
  connection->local_endpoint = listener->local_endpoint;
  connection->transport_properties = listener->transport_properties;
  connection->remote_endpoint = *remote_endpoint;
  connection->internal_connection_state = listener->socket_manager->internal_socket_manager_state;
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
  ct_connection_build_with_connection_group(connection);

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
  connection->internal_connection_state = (uv_handle_t*)received_handle;

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

int ct_connection_send_to_protocol(ct_connection_t* connection,
                                   ct_message_t* message,
                                   ct_message_context_t* context) {
  int rc = connection->protocol.send(connection, message, context);
  if (rc < 0) {
    log_error("Error sending message to protocol: %d", rc);
  }
  return rc;
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

void ct_connection_abort(ct_connection_t* connection) {
  log_info("Aborting connection: %p", (void*)connection);
  ct_connection_close(connection);
}

ct_connection_t* ct_connection_clone_full(
  const ct_connection_t* source_connection,
  ct_framer_impl_t* framer,
  const ct_transport_properties_t* connection_properties) {

  ct_connection_t* new_connection = create_empty_connection_with_uuid();
  if (!new_connection) {
    log_error("Failed to allocate memory for cloned connection");
    return NULL;
  }

  if (framer != NULL) {
    log_error("Cloning with custom framer not implemented yet");
    return NULL;
  }
  if (connection_properties != NULL) {
    log_error("Cloning with custom transport properties not implemented yet");
    return NULL;
  }

  ct_connection_group_t* connection_group = source_connection->connection_group;
  int rc = ct_connection_group_add_connection(connection_group, new_connection);
  if (rc < 0) {
    log_error("Failed to add cloned connection to connection group: %d", rc);
    ct_connection_free(new_connection);
    return NULL;
  }

  new_connection->connection_group = connection_group;
  new_connection->transport_properties = source_connection->transport_properties;
  new_connection->security_parameters = source_connection->security_parameters;
  new_connection->local_endpoint = source_connection->local_endpoint;
  new_connection->remote_endpoint = source_connection->remote_endpoint;
  new_connection->protocol = source_connection->protocol;
  new_connection->framer_impl = source_connection->framer_impl;
  new_connection->open_type = source_connection->open_type;
  new_connection->connection_callbacks = source_connection->connection_callbacks;
  
  if (source_connection->socket_manager) {
    log_error("TODO: Figure out how to clone with socket manager");
    return NULL;
  }

  new_connection->protocol.clone_connection(source_connection, new_connection);

  return new_connection;
}

ct_connection_t* ct_connection_clone(ct_connection_t* source_connection) {
  return ct_connection_clone_full(source_connection, NULL, NULL);
}

int ct_connection_get_grouped_connections(
    const ct_connection_t* connection,
    ct_connection_t*** grouped_connections,
    size_t* num_connections
) {
  log_debug("Getting grouped connections for connection: %p", (void*)connection);

  ct_connection_t** group = malloc(sizeof(ct_connection_t*));
  if (!group) {
    log_error("Failed to allocate memory for grouped connections array");
    return -ENOMEM;
  }

  group[0] = (ct_connection_t*)connection;
  *grouped_connections = group;
  *num_connections = 1;

  return 0;
}

void ct_connection_close_group(ct_connection_t* connection) {
  log_info("Closing connection group for connection: %p", (void*)connection);
  ct_connection_close(connection);
}

void ct_connection_abort_group(ct_connection_t* connection) {
  log_info("Aborting connection group for connection: %p", (void*)connection);
  ct_connection_abort(connection);
}
