
#include "connection/connection.h"

#include "connection/connection_group.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "endpoint/local_endpoint.h"
#include "endpoint/remote_endpoint.h"
#include "message/message.h"
#include "message/message_context.h"
#include "util/uuid_util.h"
#include <assert.h>
#include <glib.h>
#include <logging/log.h>
#include <security_parameter/security_parameters.h>
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
    connection->properties.priority = CT_CONNECTION_DEFAULT_PRIORITY; // Default priority
    return connection;
}

ct_connection_t* ct_connection_create_server_connection(
    ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote_endpoint,
    const ct_local_endpoint_t* local_endpoint, const ct_security_parameters_t* security_parameters,
    const ct_connection_callbacks_t* connection_callbacks, ct_framer_impl_t* framer_impl) {
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
    connection->all_remote_endpoints = ct_remote_endpoints_deep_copy(remote_endpoint, 1);
    if (!connection->all_remote_endpoints) {
        log_error("Failed to copy remote endpoint to connection");
        ct_connection_free(connection);
        ct_connection_group_free(group);
        return NULL;
    }

    connection->num_remote_endpoints = 1;
    connection->active_remote_endpoint = 0;
    connection->num_local_endpoints = 1;
    connection->all_local_endpoints = ct_local_endpoint_deep_copy(local_endpoint);
    if (!connection->all_local_endpoints && connection->num_local_endpoints > 0) {
        log_error("Failed to copy local endpoint to connection");
        ct_connection_free(connection);
        ct_connection_group_free(group);
        return NULL;
    }
    connection->active_local_endpoint = 0;

    connection->role = CONNECTION_ROLE_SERVER;

    connection->security_parameters = ct_security_parameters_deep_copy(security_parameters);
    connection->framer_impl = framer_impl; // TODO - ownership here?

    if (connection_callbacks) {
        connection->connection_callbacks = *connection_callbacks;
    } else {
        log_debug("No connection callbacks provided for server connection, using empty callbacks");
    }

    ct_socket_manager_add_connection(socket_manager, connection);

    return connection;
}

ct_connection_t* ct_connection_create_client(
    const ct_protocol_impl_t* protocol_impl, ct_local_endpoint_t* local_endpoints,
    size_t num_local_endpoints, size_t local_endpoint_index, ct_remote_endpoint_t* remote_endpoints,
    size_t num_remote_endpoints, size_t remote_endpoint_index,
    const ct_security_parameters_t* security_parameters,
    const ct_connection_callbacks_t* connection_callbacks, ct_framer_impl_t* framer_impl) {
    log_debug("Creating client connection to remote endpoint");
    ct_connection_t* connection = ct_connection_create_empty_with_uuid();
    if (!connection) {
        log_error("Failed to create empty connection");
        return NULL;
    }

    connection->all_local_endpoints = local_endpoints;
    connection->active_local_endpoint = local_endpoint_index;
    connection->num_local_endpoints = num_local_endpoints;

    connection->all_remote_endpoints = remote_endpoints;
    connection->active_remote_endpoint = remote_endpoint_index;
    connection->num_remote_endpoints = num_remote_endpoints;

    ct_remote_endpoint_t* active_remote_endpoint =
        &connection->all_remote_endpoints[connection->active_remote_endpoint];

    if (active_remote_endpoint->resolved_address.ss_family != AF_INET6 &&
        active_remote_endpoint->resolved_address.ss_family != AF_INET) {
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

    socket_manager->all_connections = g_slist_prepend(socket_manager->all_connections, connection);
    connection->socket_manager = ct_socket_manager_ref(socket_manager);

    if (socket_manager->protocol_impl->protocol_enum == CT_PROTOCOL_UDP) {
        rc = socket_manager_insert_demuxed_connection(socket_manager, active_remote_endpoint,
                                                      connection);
    }
    if (rc < 0) {
        log_error("Failed to insert connection into socket manager: %d", rc);
        ct_connection_free(connection);
        return NULL;
    }

    connection->security_parameters = ct_security_parameters_deep_copy(security_parameters);
    if (connection_callbacks) {
        connection->connection_callbacks = *connection_callbacks;
    } else {
        log_debug("No connection callbacks provided for client connection, using empty callbacks");
    }
    connection->framer_impl = framer_impl; // TODO - ownership here?

    return connection;
}

ct_connection_t* ct_connection_create_clone(const ct_connection_t* source_connection,
                                            ct_socket_manager_t* socket_manager,
                                            ct_framer_impl_t* framer_impl,
                                            void* internal_connection_state) {
    ct_connection_t* clone = ct_connection_create_empty_with_uuid();
    if (!clone) {
        log_error("Failed to create empty connection for clone");
        return NULL;
    }

    clone->properties.state = CT_CONN_STATE_ESTABLISHING;
    clone->security_parameters =
        ct_security_parameters_deep_copy(source_connection->security_parameters);

    clone->all_remote_endpoints = ct_remote_endpoints_deep_copy(
        source_connection->all_remote_endpoints, source_connection->num_remote_endpoints);
    clone->num_remote_endpoints = source_connection->num_remote_endpoints;
    clone->active_remote_endpoint = source_connection->active_remote_endpoint;

    clone->all_local_endpoints = ct_local_endpoints_deep_copy(
        source_connection->all_local_endpoints, source_connection->num_local_endpoints);
    clone->num_local_endpoints = source_connection->num_local_endpoints;
    clone->active_local_endpoint = source_connection->active_local_endpoint;
    // In the cases where a socket manager isn't provided, the protocol will insert
    // a custom one to the new connection after cloning
    if (socket_manager) {
        ct_socket_manager_add_connection(socket_manager, clone);
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
    if (!connection) {
        log_error("Connection is NULL in ct_connection_set_can_receive");
        return;
    }
    log_trace("Setting canReceive to %s for connection %s", can_receive ? "true" : "false",
              connection->uuid);
    connection->properties.can_receive = can_receive;
}

void ct_connection_set_can_send(ct_connection_t* connection, bool can_send) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_set_can_send");
        return;
    }
    connection->properties.can_send = can_send;
}

void ct_connection_mark_as_established(ct_connection_t* connection) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_mark_as_established");
        return;
    }
    connection->properties.state = CT_CONN_STATE_ESTABLISHED;
    ct_connection_set_can_send(connection, true);
    ct_connection_set_can_receive(connection, true);
    log_trace("Marked connection %s as established", connection->uuid);
}

void ct_connection_mark_as_closing(ct_connection_t* connection) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_mark_as_closing");
        return;
    }
    connection->properties.state = CT_CONN_STATE_CLOSING;
    log_trace("Marked connection %s as closing", connection->uuid);
}

void ct_connection_mark_as_closed(ct_connection_t* connection) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_mark_as_closed");
        return;
    }
    connection->properties.state = CT_CONN_STATE_CLOSED;
    log_trace("Marked connection %s as closed", connection->uuid);
}

bool ct_connection_is_closing(const ct_connection_t* connection) {
    if (!connection) {
        return false;
    }
    return connection->properties.state == CT_CONN_STATE_CLOSING;
}

bool ct_connection_is_closed(const ct_connection_t* connection) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_is_closed");
        return false;
    }
    return connection->properties.state == CT_CONN_STATE_CLOSED;
}

bool ct_connection_is_established(const ct_connection_t* connection) {
    if (!connection) {
        return false;
    }
    return connection->properties.state == CT_CONN_STATE_ESTABLISHED;
}

bool ct_connection_is_establishing(const ct_connection_t* connection) {
    if (!connection) {
        return false;
    }
    return connection->properties.state == CT_CONN_STATE_ESTABLISHING;
}

bool ct_connection_is_closed_or_closing(const ct_connection_t* connection) {
    if (!connection) {
        return false;
    }
    return ct_connection_is_closed(connection) || ct_connection_is_closing(connection);
}

ct_connection_state_enum_t ct_connection_get_state(const ct_connection_t* connection) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_get_state");
        return -1;
    }
    return connection->properties.state;
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
        log_error("Connection is NULL in ct_connection_can_send");
        return false;
    }
    return connection->properties.can_send;
}

bool ct_connection_can_receive(const ct_connection_t* connection) {
    if (!connection) {
        log_error("Connection is NULL in ct_connection_can_receive");
        return false;
    }
    return connection->properties.can_receive;
}

// Passed as callbacks to framer implementations
int ct_connection_send_to_protocol(ct_connection_t* connection, ct_message_t* message,
                                   ct_message_context_t* context);

void ct_connection_deliver_to_app(ct_connection_t* connection, ct_message_t* message,
                                  ct_message_context_t* context);

int ct_send_message(ct_connection_t* connection, const ct_message_t* message) {
    return ct_send_message_full(connection, message, NULL);
}

int ct_send_message_full(ct_connection_t* connection, const ct_message_t* message,
                         const ct_message_context_t* message_context) {
    // Fail early if for example FINAL has been sent already
    log_debug("Trying to send message over connection: %s", connection->uuid);
    if (!ct_connection_can_send(connection)) {
        log_error("Connection %s cannot send messages in its current state", connection->uuid);
        return -EPIPE;
    }
    if (message_context && ct_message_properties_get_final(&message_context->message_properties)) {
        log_info("Sending FINAL message over connection %s, setting canSend to false",
                 connection->uuid);
        ct_connection_set_can_send(connection, false);
    }
    ct_message_context_t* message_context_copy = NULL;

    if (message_context) {
        message_context_copy = ct_message_context_deep_copy(message_context);
        if (!message_context_copy) {
            log_error("Failed to deep copy message context");
            return -ENOMEM;
        }
    } else {
        message_context_copy = ct_message_context_new_from_connection(connection);
        if (!message_context_copy) {
            log_error("Failed to create message context from connection");
            return -ENOMEM;
        }
    }

    // Deep copy the message so the library owns its lifetime
    // Ownership is transferred to the framer or protocol send function, so it is freed in protocol implementation
    ct_message_t* message_copy = ct_message_deep_copy(message);
    if (!message_copy) {
        log_error("Failed to deep copy message");
        ct_message_context_free(message_context_copy);
        return -ENOMEM;
    }

    int rc = 0;
    if (connection->framer_impl != NULL) {
        log_debug("User sending message on connection with framer");
        rc = connection->framer_impl->encode_message(connection, message_copy, message_context_copy,
                                                     ct_connection_send_to_protocol);
    } else {
        log_debug("User sending message on connection without framer");
        rc = ct_connection_send_to_protocol(connection, message_copy, message_context_copy);
    }

    if (rc < 0) {
        log_error("Synchronous error on sending message encode_message failed: %d", rc);
        ct_message_free(message_copy);
        ct_message_context_free(message_context_copy);
    }

    return rc;
}

int ct_receive_message(ct_connection_t* connection, ct_receive_callbacks_t receive_callbacks) {
    log_info("User attempting to receive message on connection: %s", connection->uuid);
    if (!connection->received_messages || !connection->received_callbacks) {
        log_error("ct_connection_t queues not initialized for receiving messages");
        return -EIO;
    }

    if (!g_queue_is_empty(connection->received_messages)) {
        log_debug("Calling receive callback immediately");
        ct_queued_message_t* queued_message = g_queue_pop_head(connection->received_messages);
        queued_message->context->user_receive_context = receive_callbacks.per_receive_context;
        receive_callbacks.receive_callback(connection, queued_message->message,
                                           queued_message->context);
        ct_queued_message_free_all(queued_message);

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
    log_debug("No message ready, pushing receive callback to queue %p",
              (void*)connection->received_callbacks);
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
    ct_socket_manager_close_connection(connection);
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

    if (connection->all_local_endpoints) {
        ct_local_endpoints_free(connection->all_local_endpoints, connection->num_local_endpoints);
        connection->all_local_endpoints = NULL;
    }
    if (connection->all_remote_endpoints) {
        ct_remote_endpoints_free(connection->all_remote_endpoints,
                                 connection->num_remote_endpoints);
        connection->all_remote_endpoints = NULL;
    }

    // Free security parameters (connection owns a deep copy)
    if (connection->security_parameters) {
        ct_security_parameters_free(connection->security_parameters);
        connection->security_parameters = NULL;
    }

    // This needs to happen before unreferencing socket manager, since
    // connection group needs to reach through to free connection group state
    if (connection->connection_group) {
        ct_connection_group_unref(connection);
        connection->connection_group = NULL;
    }

    // These are wrapped just for unit tests not to segfault.
    if (connection->socket_manager) {
        ct_socket_manager_free_connection_state(connection);

        ct_socket_manager_t* socket_manager = connection->socket_manager;
        if (socket_manager->all_connections) {
            socket_manager->all_connections =
                g_slist_remove(socket_manager->all_connections, connection);
        }
        ct_socket_manager_unref(socket_manager);
        connection->socket_manager = NULL;
    }
}

void ct_connection_free(ct_connection_t* connection) {
    ct_connection_free_content(connection);
    free(connection);
}

int ct_connection_send_to_protocol(ct_connection_t* connection, ct_message_t* message,
                                   ct_message_context_t* context) {
    int rc = connection->socket_manager->protocol_impl->send(connection, message, context);
    if (rc < 0) {
        log_error("Error sending message to protocol: %d", rc);
    }
    return rc;
}

void ct_connection_deliver_to_app(ct_connection_t* connection, ct_message_t* message,
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
        context->user_receive_context = receive_callback->per_receive_context;

        receive_callback->receive_callback(connection, message, context);
        free(receive_callback);
        ct_message_context_free(context);
        ct_message_free(message);
    }
}

void ct_connection_on_protocol_receive(ct_connection_t* connection, const void* data, size_t len) {
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
        connection->framer_impl->decode_data(connection, received_message, context,
                                             ct_connection_deliver_to_app);
    } else {
        // No framer - deliver directly to application
        ct_connection_deliver_to_app(connection, received_message, context);
    }
}

void ct_connection_abort(ct_connection_t* connection) {
    log_info("Aborting connection: %s", connection->uuid);
    connection->socket_manager->protocol_impl->abort(connection);
}

int ct_connection_clone_full(const ct_connection_t* source_connection, ct_framer_impl_t* framer,
                             const ct_transport_properties_t* connection_properties) {
    log_debug("Creating clone from connection: %s", source_connection->uuid);
    (void)connection_properties; // TODO - apply any overridden properties to the clone

    ct_connection_t* new_connection = ct_connection_create_clone(
        source_connection, source_connection->socket_manager, framer, NULL);
    int rc = source_connection->socket_manager->protocol_impl->clone_connection(source_connection,
                                                                                new_connection);
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
    return connection->connection_callbacks.per_connection_context;
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

const ct_remote_endpoint_t*
ct_connection_get_active_remote_endpoint(const ct_connection_t* connection) {
    if (!connection) {
        log_error("ct_connection_get_remote_endpoint called with NULL connection");
        return NULL;
    }
    return &connection->all_remote_endpoints[connection->active_remote_endpoint];
}

const ct_local_endpoint_t*
ct_connection_get_active_local_endpoint(const ct_connection_t* connection) {
    if (!connection) {
        log_error("ct_connection_get_local_endpoint called with NULL connection");
        return NULL;
    }
    return &connection->all_local_endpoints[connection->active_local_endpoint];
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

const ct_connection_properties_t*
ct_connection_get_connection_properties(const ct_connection_t* connection) {
    if (!connection || !connection->connection_group ||
        !connection->connection_group->transport_properties) {
        log_error("ct_get_connection_properties called with NULL connection");
        log_debug("Connection pointer: %p, connection group pointer: %p, transport properties "
                  "pointer: %p",
                  (void*)connection, (void*)(connection ? connection->connection_group : NULL),
                  (void*)(connection && connection->connection_group
                              ? connection->connection_group->transport_properties
                              : NULL));
        return NULL;
    }
    return &connection->connection_group->transport_properties->connection_properties;
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
        log_debug("Connection pointer: %p, socket manager pointer: %p", (void*)connection,
                  (void*)(connection ? connection->socket_manager : NULL));
        return;
    }
    connection->socket_manager->internal_socket_manager_state = socket_state;
}

void* ct_connection_get_socket_state(const ct_connection_t* connection) {
    if (!connection || !connection->socket_manager) {
        log_error("ct_connection_get_socket_state called with NULL connection");
        log_debug("Connection pointer: %p, socket manager pointer: %p", (void*)connection,
                  (void*)(connection ? connection->socket_manager : NULL));
        return NULL;
    }
    return connection->socket_manager->internal_socket_manager_state;
}

const ct_transport_properties_t*
ct_connection_get_transport_properties(const ct_connection_t* connection) {
    if (!connection || !connection->connection_group ||
        !connection->connection_group->transport_properties) {
        log_error("ct_connection_get_transport_properties called with NULL connection or missing "
                  "transport properties");
        log_debug("Connection pointer: %p, connection group pointer: %p, transport properties "
                  "pointer: %p",
                  (void*)connection, (void*)(connection ? connection->connection_group : NULL),
                  (void*)(connection && connection->connection_group
                              ? connection->connection_group->transport_properties
                              : NULL));
        return NULL;
    }
    return connection->connection_group->transport_properties;
}

int ct_connection_set_priority(ct_connection_t* connection, uint8_t priority) {
    if (!connection) {
        log_error("ct_connection_set_priority called with NULL connection");
        return -EINVAL;
    }
    int rc = ct_socket_manager_notify_protocol_of_priority_change(connection, priority);
    if (rc == -ENOTSUP) {
        log_debug(
            "Protocol does not support priority changes, ignoring ct_connection_set_priority call");
        // We are recording *intent*, so even when ignoring the value we store it
        connection->properties.priority = priority;
        // But not supporting it at the protocol level is not an actual error
        return 0;
    }
    if (rc < 0) {
        log_error("Failed to notify protocol of priority change: %d", rc);
        // But supporting it but failing to set it *is* a real error
        return rc;
    }
    // If we actually support it on the protocol level then do not set
    // it if there was an error.
    connection->properties.priority = priority;
    return 0;
}

uint8_t ct_connection_get_priority(const ct_connection_t* connection) {
    if (!connection) {
        log_error("ct_connection_get_priority called with NULL connection");
        return UINT8_MAX;
    }
    return connection->properties.priority;
}

size_t ct_connection_get_num_remote_endpoints(const ct_connection_t* connection) {
    return connection ? connection->num_remote_endpoints : 0;
}

size_t ct_connection_get_num_local_endpoints(const ct_connection_t* connection) {
    return connection ? connection->num_local_endpoints : 0;
}

const ct_remote_endpoint_t*
ct_connection_get_remote_endpoints_list(const ct_connection_t* connection) {
    return connection ? connection->all_remote_endpoints : NULL;
}

const ct_local_endpoint_t*
ct_connection_get_local_endpoints_list(const ct_connection_t* connection) {
    return connection ? connection->all_local_endpoints : NULL;
}

void ct_connection_set_active_remote_endpoint_index(ct_connection_t* connection,
                                                    size_t remote_endpoint_index) {
    assert(remote_endpoint_index < connection->num_remote_endpoints);
    connection->active_remote_endpoint = remote_endpoint_index;
}

void ct_connection_set_active_local_endpoint_index(ct_connection_t* connection,
                                                   size_t local_endpoint_index) {
    assert(local_endpoint_index < connection->num_local_endpoints);
    connection->active_local_endpoint = local_endpoint_index;
}

int ct_connection_set_active_remote_endpoint(ct_connection_t* connection,
                                             const ct_remote_endpoint_t* remote_endpoint) {
    assert(remote_endpoint->resolved_address.ss_family == AF_INET ||
           remote_endpoint->resolved_address.ss_family == AF_INET6);
    for (size_t remote_ix = 0; remote_ix < connection->num_remote_endpoints; remote_ix++) {
        if (ct_remote_endpoint_resolved_equals(remote_endpoint,
                                               &connection->all_remote_endpoints[remote_ix])) {
            connection->active_remote_endpoint = remote_ix;
            return 0;
        }
    }
    ct_remote_endpoint_t* temp =
        realloc(connection->all_remote_endpoints,
                sizeof(ct_remote_endpoint_t) * (connection->num_remote_endpoints + 1));
    if (!temp) {
        log_error("Failed to allocate memory for new remote endpoint");
        return -ENOMEM;
    }

    connection->num_remote_endpoints++;
    connection->all_remote_endpoints = temp;
    int rc = ct_remote_endpoint_copy_content(remote_endpoint,
                                             temp + connection->num_remote_endpoints - 1);
    if (rc != 0) {
        log_error("Failed to deep copy new remote endpoint: %d", rc);
        connection->num_remote_endpoints--; // Roll back the count since we failed to copy
        return rc;
    }

    ct_connection_set_active_remote_endpoint_index(connection,
                                                   connection->num_remote_endpoints - 1);
    return 0;
}

int ct_connection_set_active_local_endpoint(ct_connection_t* connection,
                                            const ct_local_endpoint_t* local_endpoint) {
    log_debug("Setting active local endpoint for connection %s", connection->uuid);
    assert(local_endpoint->resolved_address.ss_family == AF_INET ||
           local_endpoint->resolved_address.ss_family == AF_INET6);

    for (size_t local_ix = 0; local_ix < ct_connection_get_num_local_endpoints(connection);
         local_ix++) {
        if (ct_local_endpoint_resolved_equals(local_endpoint,
                                              &connection->all_local_endpoints[local_ix])) {
            log_trace("Found matching local endpoint at index %zu, setting active_local_endpoint "
                      "to this index",
                      local_ix);
            connection->active_local_endpoint = local_ix;
            return 0;
        }
    }
    log_debug("No matching local endpoint found, adding new local endpoint to list and setting it "
              "as active");
    ct_local_endpoint_t* temp =
        realloc(connection->all_local_endpoints,
                sizeof(ct_local_endpoint_t) * (connection->num_local_endpoints + 1));
    if (!temp) {
        log_error("Failed to allocate memory for new local endpoint");
        return -ENOMEM;
    }

    connection->num_local_endpoints++;
    connection->all_local_endpoints = temp;
    int rc =
        ct_local_endpoint_copy_content(local_endpoint, temp + connection->num_local_endpoints - 1);
    if (rc != 0) {
        log_error("Failed to deep copy new local endpoint: %d", rc);
        connection->num_local_endpoints--; // Roll back the count since we failed to copy
        return rc;
    }

    ct_connection_set_active_local_endpoint_index(connection, connection->num_local_endpoints - 1);

    return 0;
}

void ct_connection_set_all_local_port(ct_connection_t* connection, uint16_t port) {
    for (size_t i = 0; i < ct_connection_get_num_local_endpoints(connection); i++) {
        struct sockaddr_storage* addr = &connection->all_local_endpoints[i].resolved_address;
        if (addr->ss_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)addr;
            addr_in->sin_port = htons(port);
        } else if (addr->ss_family == AF_INET6) {
            struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)addr;
            addr_in6->sin6_port = htons(port);
        }
    }
}
