
#include "connection/connection.h"

#include "connection/connection_group.h"
#include "connection/socket_manager/socket_manager.h"
#include "transport_property/transport_properties.h"
#include "endpoint/local_endpoint.h"
#include "endpoint/remote_endpoint.h"
#include "connection/socket_manager/socket_manager.h"
#include "message/message_context.h"
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
#include <sys/socket.h>
#include <uv.h>

ct_connection_t* ct_connection_create_empty_with_uuid(void) {
  ct_connection_t* connection = malloc(sizeof(ct_connection_t));
  if (!connection) {
    log_error("Failed to allocate memory for ct_connection_t");
    return NULL;
  }
  memset(connection, 0, sizeof(ct_connection_t));
  generate_uuid_string(connection->uuid);

  connection->received_callbacks = g_queue_new();
  connection->received_messages = g_queue_new();
  connection->transport_properties = ct_transport_properties_new();
  return connection;
}

ct_connection_t* ct_connection_create_server_connection(ct_socket_manager_t* socket_manager,
                                             const ct_remote_endpoint_t* remote_endpoint,
                                             const ct_security_parameters_t* security_parameters,
                                             ct_framer_impl_t* framer_impl
                                             ) {
  log_debug("Creating server connection for remote endpoint");
  ct_connection_t* connection = ct_connection_create_empty_with_uuid();
  if (!connection) {
    log_error("Failed to create empty connection");
    return NULL;
  }
  ct_connection_group_t* group = ct_connection_group_new();
  if (!group) {
    log_error("Failed to get or create connection group for new server connection");
    ct_connection_free(connection);
    return NULL;
  }

  ct_connection_group_add_connection(group, connection);
  // > Connection Properties can be set on Connections and Preconnections; 
  // > when set on Preconnections, they act as an initial default for the resulting Connections
  connection->transport_properties = ct_transport_properties_deep_copy(&socket_manager->listener->transport_properties);
  connection->socket_manager = ct_socket_manager_ref(socket_manager);
  connection->local_endpoint = ct_local_endpoint_deep_copy(&socket_manager->listener->local_endpoint);
  connection->remote_endpoint = ct_remote_endpoint_deep_copy(remote_endpoint);
  connection->role = CONNECTION_ROLE_SERVER;

  connection->security_parameters = ct_security_parameters_deep_copy(security_parameters);
  connection->framer_impl = framer_impl; // TODO - ownership here?

  log_debug("Created new server connection: %s", connection->uuid);

  return connection;
}

ct_connection_t* ct_connection_create_client(const ct_protocol_impl_t* protocol_impl,
                                             const ct_local_endpoint_t* local_endpoint,
                                             const ct_remote_endpoint_t* remote_endpoint,
                                             const ct_transport_properties_t* transport_properties,
                                             const ct_security_parameters_t* security_parameters,
                                             const ct_connection_callbacks_t* connection_callbacks,
                                             ct_framer_impl_t* framer_impl) {
  log_debug("Creating client connection to remote endpoint");
  ct_connection_t* connection = ct_connection_create_empty_with_uuid();
  if (!connection) {
    log_error("Failed to create empty connection");
    return NULL;
  }
  if (remote_endpoint->data.resolved_address.ss_family != AF_INET6 &&
      remote_endpoint->data.resolved_address.ss_family != AF_INET) {
    log_error("Remote endpoint has unsupported address family");
    ct_connection_free(connection);
    return NULL;
  }
  ct_socket_manager_t* socket_manager = ct_socket_manager_new(protocol_impl, NULL);

  ct_connection_group_t* group = ct_connection_group_new();
  if (!group) {
    log_error("Failed to create new connection group for client connection");
    ct_connection_free(connection);
    ct_socket_manager_free(socket_manager);
    return NULL;
  }
  int rc = ct_connection_group_add_connection(group, connection);
  if (rc < 0) {
    log_error("Failed to add connection to new connection group: %d", rc);
    ct_connection_group_free(group);
    ct_connection_free(connection);
    ct_socket_manager_free(socket_manager);
    return NULL;
  }

  if (transport_properties) {
    log_debug("Copying provided transport properties for client connection");
    ct_transport_properties_free(connection->transport_properties);
    connection->transport_properties = ct_transport_properties_deep_copy(transport_properties);
  }
  if (!connection->transport_properties) {
    log_error("Failed to copy transport properties for client connection");
    ct_connection_group_free(group);
    ct_connection_free(connection);
    ct_socket_manager_free(socket_manager);
    return NULL;
  }
  connection->socket_manager = ct_socket_manager_ref(socket_manager);
  rc = socket_manager_insert_connection(socket_manager, remote_endpoint, connection);
  if (rc < 0) {
    log_error("Failed to insert connection into socket manager: %d", rc);
    ct_connection_group_free(group);
    ct_connection_free(connection);
    ct_socket_manager_unref(socket_manager);
    return NULL;
  }

  
  connection->local_endpoint = ct_local_endpoint_deep_copy(local_endpoint);
  connection->remote_endpoint = ct_remote_endpoint_deep_copy(remote_endpoint);
  connection->security_parameters = ct_security_parameters_deep_copy(security_parameters);
  if (connection_callbacks) {
    connection->connection_callbacks = *connection_callbacks;
  }
  else {
    log_debug("No connection callbacks provided for client connection, using empty callbacks");
  }
  connection->framer_impl = framer_impl; // TODO - ownership here?

  return connection;
}

ct_connection_t* ct_connection_create_clone(const ct_connection_t* source_connection,
                                            ct_socket_manager_t* socket_manager,
                                            ct_framer_impl_t* framer_impl,
                                            void* internal_connection_state
                                            ) {
  ct_connection_t* clone = ct_connection_create_empty_with_uuid();
  if (!clone) {
    log_error("Failed to create empty connection for clone");
    return NULL;
  }

  clone->transport_properties = ct_transport_properties_deep_copy(source_connection->transport_properties); // TODO - some of these should be shared
  clone->transport_properties->connection_properties.list[STATE].value.enum_val = CONN_STATE_ESTABLISHING;
  clone->security_parameters = ct_security_parameters_deep_copy(source_connection->security_parameters);
  clone->local_endpoint = ct_local_endpoint_deep_copy(source_connection->local_endpoint);
  clone->remote_endpoint = ct_remote_endpoint_deep_copy(source_connection->remote_endpoint);
  if (socket_manager) {
    log_debug("Using provided socket manager for cloned connection");
    clone->socket_manager = ct_socket_manager_ref(socket_manager);
  }
  else {
    log_debug("No socket manager provided for cloned connection, creating new socket manager with same protocol implementation");
    ct_socket_manager_t* new_socket_manager = ct_socket_manager_new(source_connection->socket_manager->protocol_impl, NULL);
    clone->socket_manager = ct_socket_manager_ref(new_socket_manager);
  }
  log_debug("Clone socket manager pointer: %p", (void*)clone->socket_manager);

  int rc = socket_manager_insert_connection(clone->socket_manager, clone->remote_endpoint, clone);
  if (rc < 0) {
    log_error("Failed to insert cloned connection into socket manager: %d", rc);
    ct_connection_free(clone);
    return NULL;
  }

  clone->role = source_connection->role;
  if (framer_impl) {
    clone->framer_impl = framer_impl;
  } else {
    clone->framer_impl = source_connection->framer_impl; // TODO - ownership here?
  }
  clone->connection_callbacks = source_connection->connection_callbacks;
  clone->internal_connection_state = internal_connection_state;

  ct_connection_group_add_connection(source_connection->connection_group, clone);
  return clone;
}

void ct_connection_set_can_receive(ct_connection_t* connection, bool can_receive) {
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_set_can_receive");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return;
  }
  log_trace("Setting canReceive to %s for connection %s", can_receive ? "true" : "false", connection->uuid);
  connection->transport_properties->connection_properties.list[CAN_RECEIVE].value.enum_val = can_receive;
}

void ct_connection_set_can_send(ct_connection_t* connection, bool can_send) {
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_set_can_send");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return;
  }
  connection->transport_properties->connection_properties.list[CAN_SEND].value.enum_val = can_send;
}

void ct_connection_mark_as_established(ct_connection_t* connection) {
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_mark_as_established");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return;
  }
  connection->transport_properties->connection_properties.list[STATE].value.enum_val = CONN_STATE_ESTABLISHED;
  ct_connection_set_can_send(connection, true);
  ct_connection_set_can_receive(connection, true);
  log_trace("Marked connection %s as established", connection->uuid);
}

void ct_connection_mark_as_closing(ct_connection_t* connection) {
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_mark_as_closing");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return;
  }
  connection->transport_properties->connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSING;
  log_trace("Marked connection %s as closing", connection->uuid);
}

void ct_connection_mark_as_closed(ct_connection_t* connection) {
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_mark_as_closed");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return;
  }
  connection->transport_properties->connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;
  log_trace("Marked connection %s as closed", connection->uuid);
}

bool ct_connection_is_closing(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties->connection_properties.list[STATE].value.enum_val == CONN_STATE_CLOSING;
}

bool ct_connection_is_closed(const ct_connection_t* connection) {
  if (!connection || !connection->transport_properties) {
    return false;
  }
  return connection->transport_properties->connection_properties.list[STATE].value.enum_val == CONN_STATE_CLOSED;
}

bool ct_connection_is_established(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties->connection_properties.list[STATE].value.enum_val == CONN_STATE_ESTABLISHED;
}

bool ct_connection_is_closed_or_closing(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return ct_connection_is_closed(connection) || ct_connection_is_closing(connection);
}

ct_connection_state_enum_t ct_connection_get_state(const ct_connection_t* connection) {
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_get_state");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return -1;
  }
  return connection->transport_properties->connection_properties.list[STATE].value.enum_val;
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
  if (!connection || !connection->transport_properties) {
    log_error("Connection or transport properties is NULL in ct_connection_can_send");
    log_debug("Connection: %p, connection->transport_properties: %p", (void*)connection, (void*)(connection ? connection->transport_properties : NULL));
    return false;
  }
  return connection->transport_properties->connection_properties.list[CAN_SEND].value.enum_val;
}

bool ct_connection_can_receive(const ct_connection_t* connection) {
  if (!connection) {
    return false;
  }
  return connection->transport_properties->connection_properties.list[CAN_RECEIVE].value.enum_val;
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
    ct_queued_message_t* queued_message = g_queue_pop_head(connection->received_messages);
    queued_message->context->user_receive_context = receive_callbacks.user_receive_context;
    receive_callbacks.receive_callback(connection, &queued_message->message, queued_message->context);
    ct_queued_message_free_ctaps_ownership(queued_message);

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

void ct_connection_close(ct_connection_t* connection) {
    log_info("Closing connection: %s", connection->uuid);
    if (ct_connection_is_closed_or_closing(connection)) {
        log_warn("Trying to close closing or closed connection: %s, ignoring", connection->uuid);
        return;
    }
    ct_connection_mark_as_closing(connection);
    ct_socket_manager_close_connection(connection->socket_manager, connection);
}

void ct_connection_free_content(ct_connection_t* connection) {
  if (!connection) {
    return;
  }
  log_debug("Freeing content of connection: %s", connection->uuid);

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
      ct_queued_message_t* q_msg = g_queue_pop_head(connection->received_messages);
      ct_queued_message_free_all(q_msg);
    }
    g_queue_free(connection->received_messages);
    connection->received_messages = NULL;
  }

  if (connection->transport_properties) {
    ct_transport_properties_free(connection->transport_properties);
    connection->transport_properties = NULL;
  }

  ct_local_endpoint_free(connection->local_endpoint);
  ct_remote_endpoint_free(connection->remote_endpoint);

  // Free security parameters (connection owns a deep copy)
  if (connection->security_parameters) {
    ct_sec_param_free(connection->security_parameters);
    connection->security_parameters = NULL;
  }

  // These are wrapped just for unit tests not to segfault.
  if (connection->socket_manager) {
    ct_socket_manager_t* socket_manager = connection->socket_manager;
    if (socket_manager->all_connections) {
      socket_manager->all_connections = g_slist_remove(socket_manager->all_connections, connection);
    }

    ct_socket_manager_unref(socket_manager);
  }
  if (connection->connection_group && connection->connection_group->connections) {
    g_hash_table_remove(connection->connection_group->connections, connection->uuid);
  }
  ct_connection_group_unref(connection->connection_group);
}

void ct_connection_free(ct_connection_t* connection) {
  ct_connection_free_content(connection);
  free(connection);
}

int ct_connection_send_to_protocol(ct_connection_t* connection,
                                   ct_message_t* message,
                                   ct_message_context_t* context) {
  int rc = connection->socket_manager->protocol_impl->send(connection, message, context);
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
    ct_queued_message_t* queued_message = ct_queued_message_new(message, context);
    g_queue_push_tail(connection->received_messages, queued_message);
  } else {
    log_debug("Receive callback ready for connection: %s, calling it", connection->uuid);
    ct_receive_callbacks_t* receive_callback = g_queue_pop_head(connection->received_callbacks);

    if (!context) {
      log_warn("Message context is NULL, allocating new context");
      context = ct_message_context_new_from_connection(connection);
      if (!context) {
        log_error("Failed to allocate memory for message context");
        free(receive_callback);
        return;
      }
    }
    context->user_receive_context = receive_callback->user_receive_context;

    receive_callback->receive_callback(connection, &message, context);
    free(receive_callback);
    ct_message_context_free(context);
  }
}

void ct_connection_on_protocol_receive(ct_connection_t* connection,
                                       const void* data,
                                       size_t len) {
  ct_message_t* received_message = ct_message_new_with_content(data, len);
  if (!received_message) {
    log_error("Failed to allocate memory for received message");
    return;
  }
  ct_message_context_t* context = ct_message_context_new_from_connection(connection);
  if (!context) {
    log_error("Failed to allocate memory for message context");
    ct_message_free(received_message);
    return;
  }

  if (connection->framer_impl != NULL) {
    // Framer present - let it decode, it will call ct_connection_deliver_to_app()
    connection->framer_impl->decode_data(connection, received_message, context, ct_connection_deliver_to_app);
  } else {
    // No framer - deliver directly to application
    ct_connection_deliver_to_app(connection, received_message, context);
  }
}

void ct_connection_abort(ct_connection_t* connection) {
  log_info("Aborting connection: %s", connection->uuid);
  connection->socket_manager->protocol_impl->abort(connection);
}

int ct_connection_clone_full(
  const ct_connection_t* source_connection,
  ct_framer_impl_t* framer,
  const ct_transport_properties_t* connection_properties) {
  log_debug("Creating clone from connection: %s", source_connection->uuid);
  (void)connection_properties; // TODO - apply any overridden properties to the clone

  ct_connection_t* new_connection = ct_connection_create_clone(source_connection, NULL, framer, NULL);
  int rc = new_connection->socket_manager->protocol_impl->clone_connection(source_connection, new_connection);
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
  return connection->socket_manager->protocol_impl->name;
}

const ct_remote_endpoint_t* ct_connection_get_remote_endpoint(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_get_remote_endpoint called with NULL connection");
    return NULL;
  }
  return connection->remote_endpoint;
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

const ct_connection_properties_t* ct_connection_get_connection_properties(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_get_connection_properties called with NULL connection");
    return NULL;
  }
  return &connection->transport_properties->connection_properties;
}

void connection_set_resolved_local_address(ct_connection_t* connection, const struct sockaddr_storage* addr) {
  memcpy(&connection->local_endpoint->data.resolved_address, addr, sizeof(struct sockaddr_storage));
}

ct_protocol_enum_t ct_connection_get_transport_protocol(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_get_transport_protocol called with NULL connection");
    return CT_PROTOCOL_ERROR;
  }
  return connection->socket_manager->protocol_impl->protocol_enum;
}

bool ct_connection_sent_early_data(const ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_used_0rtt called with NULL connection");
    return false;
  }
  return connection->sent_early_data;
}

void ct_connection_set_sent_early_data(ct_connection_t* connection, bool used_0rtt) {
  if (!connection) {
    log_error("ct_connection_set_used_0rtt called with NULL connection");
    return;
  }
  connection->sent_early_data = used_0rtt;
}

void ct_connection_set_socket_state(ct_connection_t* connection, void* socket_state) {
  if (!connection || !connection->socket_manager) {
    log_error("ct_connection_set_socket_state called with NULL connection");
    log_debug("Connection pointer: %p, socket manager pointer: %p", (void*)connection, (void*)(connection ? connection->socket_manager : NULL));
    return;
  }
  connection->socket_manager->internal_socket_manager_state = socket_state;
}

void* ct_connection_get_socket_state(ct_connection_t* connection) {
  if (!connection || !connection->socket_manager) {
    log_error("ct_connection_get_socket_state called with NULL connection");
    log_debug("Connection pointer: %p, socket manager pointer: %p", (void*)connection, (void*)(connection ? connection->socket_manager : NULL));
    return NULL;
  }
  return connection->socket_manager->internal_socket_manager_state;
}
