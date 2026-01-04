
#include "connection/connection.h"

#include "connection/connection_group.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "message/message.h"
#include "util/uuid_util.h"
#include <security_parameter/security_parameters.h>
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

int ct_connection_build_with_new_connection_group(ct_connection_t* connection) {
  memset(connection, 0, sizeof(ct_connection_t));
  generate_uuid_string(connection->uuid);

  // Create connection group for this connection
  ct_connection_group_t* group = malloc(sizeof(ct_connection_group_t));
  if (!group) {
    log_error("Failed to allocate connection group");
    return -ENOMEM;
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

  connection->framer_impl = NULL;
  connection->connection_group = group;
  connection->received_messages = g_queue_new();
  connection->received_callbacks = g_queue_new();
  return 0;
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

void ct_connection_set_can_receive(ct_connection_t* connection, bool can_receive) {
  connection->transport_properties.connection_properties.list[CAN_RECEIVE].value.enum_val = can_receive;
}

void ct_connection_set_can_send(ct_connection_t* connection, bool can_send) {
  connection->transport_properties.connection_properties.list[CAN_SEND].value.enum_val = can_send;
}

void ct_connection_mark_as_established(ct_connection_t* connection) {
  connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_ESTABLISHED;
  ct_connection_set_can_send(connection, true);
  ct_connection_set_can_receive(connection, true);
  log_trace("Marked connection %s as established", connection->uuid);
}

void ct_connection_mark_as_closing(ct_connection_t* connection) {
  connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSING;
  log_trace("Marked connection %s as closing", connection->uuid);
}

void ct_connection_mark_as_closed(ct_connection_t* connection) {
  connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;
  log_trace("Marked connection %s as closed", connection->uuid);
}

bool ct_connection_is_closing(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties.connection_properties.list[STATE].value.enum_val == CONN_STATE_CLOSING;
}

bool ct_connection_is_closed(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties.connection_properties.list[STATE].value.enum_val == CONN_STATE_CLOSED;
}

bool ct_connection_is_established(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties.connection_properties.list[STATE].value.enum_val == CONN_STATE_ESTABLISHED;
}

bool ct_connection_is_closed_or_closing(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return ct_connection_is_closed(connection) || ct_connection_is_closing(connection);
}

bool ct_connection_is_client(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->role == CONNECTION_ROLE_CLIENT;
}

bool ct_connection_is_server(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->role == CONNECTION_ROLE_SERVER;
}

bool ct_connection_can_send(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties.connection_properties.list[CAN_SEND].value.enum_val;
}

bool ct_connection_can_receive(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties.connection_properties.list[CAN_RECEIVE].value.enum_val;
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
  // Fail early if for example FINAL has been sent already
  log_debug("Trying to send message over connection: %s", connection->uuid);
  if (!ct_connection_can_send(connection)) {
    log_error("Connection %s cannot send messages in its current state", connection->uuid);
    return -EPIPE;
  }
  if (message_context && ct_message_properties_is_final(&message_context->message_properties)) {
    log_info("Sending FINAL message over connection %s, setting canSend to false", connection->uuid);
    ct_connection_set_can_send(connection, false);
  }
  // Deep copy the message so the library owns its lifetime
  // Ownership is transferred to the framer or protocol send function, so it is freed in protocol implementation
  ct_message_t* message_copy = ct_message_deep_copy(message);
  if (!message_copy) {
    log_error("Failed to deep copy message");
    return -ENOMEM;
  }

  int rc = 0;
  if (connection->framer_impl != NULL) {
    log_debug("User sending message on connection with framer");
    rc = connection->framer_impl->encode_message(connection, message_copy, message_context, ct_connection_send_to_protocol);
    if (rc < 0) {
      log_error("Framer encode_message failed: %d", rc);
    }
  } else {
    log_debug("User sending message on connection without framer");
    rc = ct_connection_send_to_protocol(connection, message_copy, message_context);
  }

  return rc;
}

int ct_receive_message(ct_connection_t* connection,
                    ct_receive_callbacks_t receive_callbacks
                    ) {
  log_info("User attempting to receive message on connection: %s", connection->uuid);
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

int ct_connection_build_multiplexed(ct_connection_t* connection, const ct_listener_t* listener, const ct_remote_endpoint_t* remote_endpoint) {
  int rc = ct_connection_build_with_new_connection_group(connection);
  if (rc < 0) {
    log_error("Failed to build connection with connection group, error code: %d", rc);
    return rc;
  }

  connection->local_endpoint = listener->local_endpoint;
  connection->transport_properties = listener->transport_properties;
  connection->remote_endpoint = *remote_endpoint;
  connection->internal_connection_state = listener->socket_manager->internal_socket_manager_state;
  connection->protocol = listener->socket_manager->protocol_impl;
  connection->socket_manager = listener->socket_manager;
  connection->security_parameters = ct_security_parameters_deep_copy(listener->security_parameters);
  connection->socket_type = CONNECTION_SOCKET_TYPE_MULTIPLEXED;
  connection->role = CONNECTION_ROLE_SERVER;
  return 0;
}

ct_connection_t* ct_connection_create_clone(const ct_connection_t* src_clone) {
  log_debug("Creating cloned connection from source: %s", src_clone->uuid);

  // Allocate new connection with UUID
  ct_connection_t* dest_clone = create_empty_connection_with_uuid();
  if (!dest_clone) {
    log_error("Failed to allocate memory for cloned connection");
    return NULL;
  }

  // Copy properties from source
  dest_clone->connection_group = src_clone->connection_group;
  dest_clone->local_endpoint = src_clone->local_endpoint;
  dest_clone->remote_endpoint = src_clone->remote_endpoint;
  dest_clone->transport_properties = src_clone->transport_properties;
  dest_clone->security_parameters = ct_security_parameters_deep_copy(src_clone->security_parameters);
  dest_clone->protocol = src_clone->protocol;
  dest_clone->framer_impl = src_clone->framer_impl;
  dest_clone->socket_manager = src_clone->socket_manager;
  dest_clone->socket_type = src_clone->socket_type;
  dest_clone->role = src_clone->role;

  // Initialize new queues for this connection
  dest_clone->received_callbacks = g_queue_new();
  dest_clone->received_messages = g_queue_new();

  // Add to the connection group
  int rc = ct_connection_group_add_connection(dest_clone->connection_group, dest_clone);
  if (rc < 0) {
    log_error("Failed to add cloned connection to group: %d", rc);
    ct_connection_free(dest_clone);
    return NULL;
  }

  // Initialize protocol-specific state (e.g., QUIC stream state)
  if (src_clone->protocol.clone_connection) {
    rc = src_clone->protocol.clone_connection(src_clone, dest_clone);
    if (rc < 0) {
      log_error("Failed to initialize protocol state for cloned connection: %d", rc);
      ct_connection_free(dest_clone);
      return NULL;
    }
  }

  log_debug("Successfully created cloned connection: %s", dest_clone->uuid);
  return dest_clone;
}

void ct_connection_close(ct_connection_t* connection) {
  log_info("Closing connection: %s", connection->uuid);

  // Always let the protocol handle the close logic
  // For protocols like QUIC, this will initiate close handshake
  // The protocol is responsible for cleaning up (removing from socket manager, etc.)
  if (ct_connection_is_closed_or_closing(connection)) {
    log_warn("Trying to close closing or closed connection: %s, ignoring", connection->uuid);
    return;
  }
  ct_connection_mark_as_closing(connection);
  int rc = connection->protocol.close(connection);
  if (rc < 0) {
    log_error("Error closing connection: %d", rc);
  }
}

ct_connection_t* ct_connection_build_from_received_handle(const struct ct_listener_s* listener, uv_stream_t* received_handle) {
  log_debug("Building ct_connection_t from received handle");
  ct_connection_t* connection = malloc(sizeof(ct_connection_t));
  if (!connection) {
    log_error("Failed to allocate memory for ct_connection_t");
    return NULL;
  }
  ct_connection_build_with_new_connection_group(connection);

  connection->transport_properties = listener->transport_properties;
  connection->local_endpoint = listener->local_endpoint;
  int rc = listener->socket_manager->protocol_impl.remote_endpoint_from_peer((uv_handle_t*)received_handle, &connection->remote_endpoint);
  if (rc < 0) {
    log_error("Could not build remote endpoint from received handle's remote address");
    free(connection);
    return NULL;
  }

  connection->protocol = listener->socket_manager->protocol_impl;
  connection->socket_type = CONNECTION_SOCKET_TYPE_STANDALONE;
  connection->role = CONNECTION_ROLE_SERVER;
  connection->internal_connection_state = (uv_handle_t*)received_handle;

  return connection;
}

void ct_connection_free_content(ct_connection_t* connection) {
  if (!connection) {
    return;
  }

  if (connection->received_callbacks) {
    // Free any pending callbacks in the queue
    while (!g_queue_is_empty(connection->received_callbacks)) {
      ct_receive_callbacks_t* callback = g_queue_pop_head(connection->received_callbacks);
      if (callback) {
        free(callback);
      }
    }
    g_queue_free(connection->received_callbacks);
    connection->received_callbacks = NULL;
  }

  if (connection->received_messages) {
    // Free any pending messages in the queue
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
    connection->received_messages = NULL;
  }

  ct_local_endpoint_free_strings(&connection->local_endpoint);
  ct_remote_endpoint_free_strings(&connection->remote_endpoint);

  // Free security parameters (connection owns a deep copy)
  if (connection->security_parameters) {
    ct_security_parameters_free(connection->security_parameters);
    connection->security_parameters = NULL;
  }

  // Remove connection from its group and free the group if this was the last connection
  ct_connection_group_t* group = ct_connection_get_connection_group(connection);
  if (group) {
    // Remove this connection from the group
    ct_connection_group_remove_connection(group, connection);

    // If the group is now empty, free it
    if (ct_connection_group_is_empty(group)) {
      log_debug("Connection group %s is now empty, freeing it", group->connection_group_id);
      ct_connection_group_free(group);
    }

    connection->connection_group = NULL;
  }
}

void ct_connection_free(ct_connection_t* connection) {
  ct_connection_free_content(connection);
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
  log_info("Aborting connection: %s", connection->uuid);
  connection->protocol.abort(connection);
}

int ct_connection_clone_full(
  const ct_connection_t* source_connection,
  ct_framer_impl_t* framer,
  const ct_transport_properties_t* connection_properties) {
  log_debug("Creating clone from connection: %s", source_connection->uuid);

  ct_connection_t* new_connection = create_empty_connection_with_uuid();
  if (!new_connection) {
    log_error("Failed to allocate memory for cloned connection");
    return -ENOMEM;
  }

  if (framer != NULL) {
    log_error("Cloning with custom framer not implemented yet");
    return -ENOSYS;
  }
  if (connection_properties != NULL) {
    log_error("Cloning with custom transport properties not implemented yet");
    return -ENOSYS;
  }

  ct_connection_group_t* connection_group = source_connection->connection_group;
  int rc = ct_connection_group_add_connection(connection_group, new_connection);
  if (rc < 0) {
    log_error("Failed to add cloned connection to connection group: %d", rc);
    ct_connection_free(new_connection);
    return rc;
  }

  new_connection->connection_group = connection_group;
  new_connection->transport_properties = source_connection->transport_properties;
  new_connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_ESTABLISHING;
  new_connection->security_parameters = ct_security_parameters_deep_copy(source_connection->security_parameters);
  new_connection->local_endpoint = source_connection->local_endpoint;
  new_connection->remote_endpoint = source_connection->remote_endpoint;
  new_connection->protocol = source_connection->protocol;
  new_connection->framer_impl = source_connection->framer_impl;
  new_connection->socket_type = source_connection->socket_type;
  new_connection->role = source_connection->role;
  new_connection->connection_callbacks = source_connection->connection_callbacks;

  if (source_connection->socket_manager) {
    log_error("TODO: Figure out how to clone with socket manager");
    return -ENOSYS;
  }

  if (new_connection->protocol.clone_connection == NULL) {
    log_error("Protocol has not implemented connection cloning");
    ct_connection_free(new_connection);
    return -ENOSYS;
  }
  rc = new_connection->protocol.clone_connection(source_connection, new_connection);
  if (rc < 0) {
    log_error("Failed to initialize protocol state for cloned connection: %d", rc);
    ct_connection_free(new_connection);
    return rc;
  }

  return 0;
}

int ct_connection_clone(ct_connection_t* source_connection) {
  return ct_connection_clone_full(source_connection, NULL, NULL);
}

void* ct_connection_get_callback_context(const ct_connection_t* connection) {
  if (!connection) {
    return NULL;
  }
  return connection->connection_callbacks.user_connection_context;
}

const char* ct_connection_get_uuid(const ct_connection_t* connection) {
  return connection->uuid;
}

size_t ct_connection_get_total_num_grouped_connections(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_get_total_num_grouped_connections called with NULL parameter");
    return 0;
  }

  ct_connection_group_t* group = connection->connection_group;
  if (!group || !group->connections) {
    log_error("Connection %s has no valid connection group", connection->uuid);
    return 0;
  }

  return g_hash_table_size(group->connections);
}

size_t ct_connection_get_num_open_grouped_connections(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_get_num_open_grouped_connections called with NULL parameter");
    return 0;
  }

  ct_connection_group_t* group = connection->connection_group;
  if (!group || !group->connections) {
    log_error("Connection %s has no valid connection group", connection->uuid);
    return 0;
  }

  size_t num_active = 0;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init(&iter, group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* conn = (ct_connection_t*)value;
    if (!ct_connection_is_closed(conn)) {
      num_active++;
    }
  }
  return num_active;
}

const char* ct_connection_get_protocol_name(const ct_connection_t* connection) {
  if (!connection) {
    return NULL;
  }
  return connection->protocol.name;
}

const ct_remote_endpoint_t* ct_connection_get_remote_endpoint(const ct_connection_t* connection) {
  return &connection->remote_endpoint;
}

ct_connection_group_t* ct_connection_get_connection_group(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_get_connection_group called with NULL connection");
    return NULL;
  }
  if (!connection->connection_group) {
    log_error("Connection has no connection group");
    return NULL;
  }
  return connection->connection_group;
}
