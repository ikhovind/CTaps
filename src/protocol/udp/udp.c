#include "udp.h"

#include "connection/connection.h"
#include "connection/listener.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <assert.h>
#include <glib.h>
#include <logging/log.h>
#include <protocol/common/socket_utils.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <uv.h>

// Protocol interface definition (moved from header to access internal struct)
const ct_protocol_impl_t
    udp_protocol_interface =
        {
            .name = "UDP",
            .protocol_enum = CT_PROTOCOL_UDP,
            .supports_alpn = false,
            .selection_properties =
                {.list =
                     {
                         [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
                         [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = REQUIRE}},
                         [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
                         [PRESERVE_ORDER] = {.value = {.simple_preference = PROHIBIT}},
                         [ZERO_RTT_MSG] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [MULTISTREAMING] = {.value = {.simple_preference = PROHIBIT}},
                         [FULL_CHECKSUM_SEND] = {.value = {.simple_preference = REQUIRE}},
                         [FULL_CHECKSUM_RECV] = {.value = {.simple_preference = REQUIRE}},
                         [CONGESTION_CONTROL] = {.value = {.simple_preference = PROHIBIT}},
                         [KEEP_ALIVE] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [INTERFACE] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [PVD] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [USE_TEMPORARY_LOCAL_ADDRESS] = {.value = {.simple_preference =
                                                                        NO_PREFERENCE}},
                         [MULTIPATH] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [ADVERTISES_ALT_ADDRES] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [DIRECTION] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [SOFT_ERROR_NOTIFY] = {.value = {.simple_preference = NO_PREFERENCE}},
                         [ACTIVE_READ_BEFORE_SEND] = {.value = {.simple_preference =
                                                                    NO_PREFERENCE}},
                     }},
            .init = udp_init,
            .init_with_send = udp_init_with_send,
            .send = udp_send,
            .listen = udp_listen,
            .close_listener = udp_close_listener,
            .close_connection = udp_close,
            .close_socket = udp_close_socket,
            .abort = udp_abort,
            .clone_connection = udp_clone_connection,
            .free_connection_state = udp_free_state,
            .free_socket_state = udp_free_socket_state,
            .close_connection_group = udp_close_connection_group,
            .free_connection_group_state = udp_free_connection_group_state,
};

udp_send_data_t* udp_send_data_new(ct_connection_t* connection, ct_message_t* message,
                                   ct_message_context_t* message_context) {
    udp_send_data_t* send_data = malloc(sizeof(udp_send_data_t));
    if (!send_data) {
        log_error("Failed to allocate memory for UDP send data");
        return NULL;
    }
    send_data->connection = connection;
    send_data->message = message;
    send_data->message_context = message_context;
    return send_data;
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    (void)handle;
    *buf = uv_buf_init(malloc(suggested_size), suggested_size);
}

void udp_multiplex_received_message(ct_socket_manager_t* socket_manager, char* buf, size_t len,
                                    const struct sockaddr_storage* remote_addr) {
    log_trace("UDP listener received message, demultiplexing to connection");

    ct_connection_t* connection = socket_manager_get_from_demux_table(socket_manager, remote_addr);

    if (!connection) {
        if (!socket_manager->listener || ct_listener_is_closed(socket_manager->listener)) {
            log_debug("Received UDP message from new connection but listener is closed, dropping");
            return;
        }

        log_debug("Did not find remote endpoint in existing connections, creating new connection "
                  "for received UDP message");

        ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
        ct_remote_endpoint_from_sockaddr(remote_endpoint, remote_addr);
        connection = ct_connection_create_server_connection(
            socket_manager, remote_endpoint, socket_manager->listener->local_endpoint,
            socket_manager->listener->security_parameters,
            &socket_manager->listener->connection_callbacks, NULL);
        ct_remote_endpoint_free(remote_endpoint);

        if (!connection) {
            log_error("Failed to build connection from received UDP message");
            // TODO - call error callback?
            return;
        }
        log_debug("Created new connection UDP connection: %s for connection received from listener",
                  connection->uuid);
        ct_connection_set_can_send(connection, true);
        ct_connection_set_can_receive(connection, true);

        socket_manager_insert_demuxed_connection(
            socket_manager, ct_connection_get_active_remote_endpoint(connection), connection);

        ct_udp_socket_state_t* socket_state =
            (ct_udp_socket_state_t*)socket_manager->internal_socket_manager_state;
        log_debug("Calling connection received for UDP connection with handle: %p",
                  socket_state->udp_handle);
        socket_manager->callbacks.connection_received(socket_manager->listener, connection);
    }
    ct_connection_on_protocol_receive(connection, buf, len);
}

void on_send(uv_udp_send_t* req, int status) {
    log_debug("UDP send callback invoked with status: %d", status);
    udp_send_data_t* send_data = req->data;
    ct_socket_manager_t* socket_manager = send_data->connection->socket_manager;
    ct_connection_t* connection = send_data->connection;
    ct_message_context_t* message_context = send_data->message_context;
    // message context is freed by socket manager
    ct_message_free(send_data->message);
    free(send_data);
    free(req);
    if (status) {
        log_error("Send error for UDP: %s\n", uv_strerror(status));
        socket_manager->callbacks.message_send_error(connection, message_context, status);
    } else {
        socket_manager->callbacks.message_sent(connection, message_context);
    }
}

void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr,
             unsigned flags) {
    (void)flags;
    if (nread < 0) {
        log_error("Read error: %s\n", uv_strerror(nread));
        uv_close((uv_handle_t*)handle, NULL);
        free(buf->base);
        return;
    }

    if (!addr) {
        // No more data to read, or an empty packet.
        if (buf->base) {
            free(buf->base);
        }
        return;
    }

    log_debug("Received message over UDP handle: %p", handle);
    ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)handle->data;
    ct_connection_t* connection =
        socket_manager_get_from_demux_table(socket_manager, (const struct sockaddr_storage*)addr);
    if (!connection) {
        log_error("Received UDP message from unknown remote endpoint, dropping");
        free(buf->base);
        return;
    }

    // Delegate to connection receive handler (handles framing if present)
    ct_connection_on_protocol_receive(connection, buf->base, nread);
    free(buf->base);
}

void closed_handle_cb(uv_handle_t* handle) {
    log_info("UDP handle closed callback invoked with handle: %p", handle);
    ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)handle->data;
    // If we aborted this handle, then any connection relying on this handle
    // is aborted as well. However we not unref the socket manager, since that
    // is related to freeing, not closing!
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, socket_manager->demux_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ct_connection_t* connection = (ct_connection_t*)value;
        if (!ct_connection_is_closed(connection)) {
            log_trace("Closing connection: %s associated with closed socket", connection->uuid);
            ct_connection_mark_as_closed(connection);
            if (connection->connection_callbacks.closed) {
                connection->connection_callbacks.closed(connection);
            } else {
                log_debug("No connection closed callback set for UDP connection %s",
                          connection->uuid);
            }
        }
    }
}

int udp_init_with_send(ct_connection_t* connection, ct_message_t* initial_message,
                       ct_message_context_t* initial_message_context) {
    log_debug("Initiating UDP connection\n");

    ct_socket_manager_t* socket_manager = connection->socket_manager;
    uv_udp_t* new_udp_handle = create_udp_listening_on_local(
        ct_connection_get_active_local_endpoint(connection), alloc_buffer, on_read);
    if (!new_udp_handle) {
        log_error("Failed to create UDP handle for connection");
        return -EIO;
    }

    int rc = resolve_local_endpoint_from_handle((uv_handle_t*)new_udp_handle, connection);
    if (rc < 0) {
        log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
        uv_close((uv_handle_t*)new_udp_handle, closed_handle_cb);
        return rc;
    }

    ct_udp_socket_state_t* socket_state = ct_udp_socket_state_new(new_udp_handle);

    ct_connection_set_socket_state(connection, socket_state);
    new_udp_handle->data = socket_manager;

    if (initial_message) {
        udp_send(connection, initial_message, initial_message_context);
    }

    socket_manager->callbacks.connection_ready(connection);
    return 0;
}

int udp_init(ct_connection_t* connection) {
    return udp_init_with_send(connection, NULL, NULL);
}

int udp_close(ct_connection_t* connection) {
    log_debug("Closing UDP connection");
    // no-op since UDP is connectionless and is therefore closed
    // whenever it is marked as "closed" by the socket manager, since we then
    // will filter out any further messages
    connection->socket_manager->callbacks.closed_connection(connection);
    return 0;
}

void udp_abort(ct_connection_t* connection) {
    log_debug("Aborting UDP connection");
    // no-op since UDP is connectionless and is therefore closed
    // whenever it is marked as "closed" by the socket manager, since we then
    // will filter out any further messages
    ct_socket_manager_t* socket_manager = connection->socket_manager;
    socket_manager->callbacks.aborted_connection(connection);
}

void udp_close_listener(struct ct_socket_manager_s* socket_manager) {
    ct_udp_socket_state_t* socket_state =
        (ct_udp_socket_state_t*)socket_manager->internal_socket_manager_state;
    log_debug("Stopping UDP listen on udp handle: %p", socket_state->udp_handle);

    // no-op since the socket is shared between listener and connections
    // The socket is instead closed when the socket manager sees no
    // more open connections
    socket_manager->callbacks.closed_listener(socket_manager);
}

int udp_send(ct_connection_t* connection, ct_message_t* message,
             ct_message_context_t* message_context) {
    (void)message_context;
    log_debug("Sending message over UDP");

    // Use the message content directly as the send buffer (it's already heap-allocated)
    uv_buf_t buffer = uv_buf_init(message->content, message->length);

    uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
    if (!send_req) {
        log_error("Failed to allocate send request\n");
        return -ENOMEM;
    }

    // Store the message in send_req->data so we can free it in the callback
    send_req->data = udp_send_data_new(connection, message, message_context);
    ct_udp_socket_state_t* socket_state = ct_connection_get_socket_state(connection);

    int rc =
        uv_udp_send(send_req, socket_state->udp_handle, &buffer, 1,
                    (const struct sockaddr*)&ct_connection_get_active_remote_endpoint(connection)
                        ->resolved_address,
                    on_send);

    if (rc < 0) {
        log_error("Error sending UDP message: %s", uv_strerror(rc));
        // Caller frees on sync error
        free(send_req->data);
        free(send_req);
    }

    return rc;
}

void socket_listen_callback(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                            const struct sockaddr* addr, unsigned flags) {
    (void)flags;
    if (nread == 0 && !addr) {
        if (buf->base) {
            free(buf->base);
        }
        return;
    }
    log_debug("UDP listen callback invoked with nread: %zd", nread);

    if (nread < 0) {
        log_error("Read error in socket_listen_callback: %s\n", uv_strerror(nread));
        free(buf->base);
        return;
    }

    ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)handle->data;

    udp_multiplex_received_message(socket_manager, buf->base, (size_t)nread,
                                   (struct sockaddr_storage*)addr);
    // When buf is passed up to connection, connection.c copies the content into a message, so
    // we can safely free the buffer here
    free(buf->base);
}

int udp_listen(ct_socket_manager_t* socket_manager) {
    log_debug("Listening via UDP");

    const ct_local_endpoint_t* local_endpoint =
        ct_listener_get_local_endpoint(socket_manager->listener);
    uv_udp_t* udp_handle =
        create_udp_listening_on_local(local_endpoint, alloc_buffer, socket_listen_callback);
    if (!udp_handle) {
        log_error("Failed to create UDP handle for listening");
        return -EIO;
    }

    udp_handle->data = socket_manager;
    ct_udp_socket_state_t* socket_state = ct_udp_socket_state_new(udp_handle);
    socket_manager->internal_socket_manager_state = socket_state;

    return 0;
}

int udp_clone_connection(const struct ct_connection_s* source_connection,
                         struct ct_connection_s* target_connection) {
    // Create ephemeral local port
    uv_udp_t* new_udp_handle = create_udp_listening_on_ephemeral(alloc_buffer, on_read);
    ct_udp_socket_state_t* socket_state = ct_udp_socket_state_new(new_udp_handle);

    if (target_connection->socket_manager) {
        ct_socket_manager_t* socket_manager = target_connection->socket_manager;
        ct_socket_manager_unref(socket_manager);
        socket_manager->all_connections =
            g_slist_remove(socket_manager->all_connections, target_connection);
    }

    ct_socket_manager_t* clone_socket_manager =
        ct_socket_manager_new(source_connection->socket_manager->protocol_impl, NULL);
    ct_socket_manager_add_connection(clone_socket_manager, target_connection);
    socket_manager_insert_demuxed_connection(
        clone_socket_manager, ct_connection_get_active_remote_endpoint(target_connection),
        target_connection);

    ct_connection_set_socket_state(target_connection, socket_state);
    new_udp_handle->data = target_connection->socket_manager;

    int rc = resolve_local_endpoint_from_handle((uv_handle_t*)new_udp_handle, target_connection);
    if (rc < 0) {
        log_error("Failed to get UDP socket name for cloned connection: %s", uv_strerror(rc));
        uv_close((uv_handle_t*)new_udp_handle, closed_handle_cb);
        return rc;
    }
    clone_socket_manager->callbacks.connection_ready(target_connection);

    return 0;
}

void udp_free_state(ct_connection_t* connection) {
    free(connection->internal_connection_state);
}

void udp_free_connection_group_state(ct_connection_group_t* connection_group) {
    (void)connection_group;
}

ct_udp_socket_state_t* ct_udp_socket_state_new(uv_udp_t* udp_handle) {
    ct_udp_socket_state_t* state = malloc(sizeof(ct_udp_socket_state_t));
    if (!state) {
        log_error("Failed to allocate memory for UDP socket state");
        return NULL;
    }
    memset(state, 0, sizeof(ct_udp_socket_state_t));
    state->udp_handle = udp_handle;
    return state;
}

void socket_closed_success(uv_handle_t* handle) {
    log_debug("UDP socket closed successfully");
    ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)handle->data;
    socket_manager->callbacks.socket_closed(socket_manager);
}

void udp_close_socket(ct_socket_manager_t* socket_manager) {
    log_debug("Closing UDP socket");
    ct_udp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
    int rc = uv_udp_recv_stop(socket_state->udp_handle);
    // This only fails on wrong handle type, so it must hold
    assert(rc == 0);
    uv_close((uv_handle_t*)socket_state->udp_handle, socket_closed_success);
}

void udp_free_socket_state(ct_socket_manager_t* socket_manager) {
    ct_udp_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
    free(socket_state->udp_handle);
    free(socket_state);
}

void udp_close_connection_group(ct_connection_group_t* connection_group) {
    // Intermediate to avoid concurrent modification
    GSList* connections = NULL;
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, connection_group->connections);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ct_connection_t* connection = (ct_connection_t*)value;
        connections = g_slist_append(connections, connection);
    }
    for (GSList* node = connections; node != NULL; node = node->next) {
        ct_connection_t* connection = (ct_connection_t*)node->data;
        udp_close(connection);
    }
}
