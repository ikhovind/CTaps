#include "quic.h"

#include "connection/connection.h"
#include "connection/connection_group.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "protocol/common/socket_utils.h"
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picotls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <uv.h>

#define MAX_QUIC_PACKET_SIZE 1500

#define MICRO_TO_MILLI(us) ((us) / 1000)

// Protocol interface definition (moved from header to access internal struct)
const ct_protocol_impl_t quic_protocol_interface = {
    .name = "QUIC",
    .protocol_enum = CT_PROTOCOL_QUIC,
    .supports_alpn = true,
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = REQUIRE}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PREFER}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = REQUIRE}},
        [ZERO_RTT_MSG] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTISTREAMING] = {.value = {.simple_preference = NO_PREFERENCE}},
        [FULL_CHECKSUM_SEND] = {.value = {.simple_preference = REQUIRE}},
        [FULL_CHECKSUM_RECV] = {.value = {.simple_preference = REQUIRE}},
        [CONGESTION_CONTROL] = {.value = {.simple_preference = REQUIRE}},
        [KEEP_ALIVE] = {.value = {.simple_preference = NO_PREFERENCE}},
        [INTERFACE] = {.value = {.simple_preference = NO_PREFERENCE}},
        [PVD] = {.value = {.simple_preference = NO_PREFERENCE}},
        [USE_TEMPORARY_LOCAL_ADDRESS] = {.value = {.simple_preference = NO_PREFERENCE}},
        [MULTIPATH] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ADVERTISES_ALT_ADDRES] = {.value = {.simple_preference = NO_PREFERENCE}},
        [DIRECTION] = {.value = {.simple_preference = NO_PREFERENCE}},
        [SOFT_ERROR_NOTIFY] = {.value = {.simple_preference = NO_PREFERENCE}},
        [ACTIVE_READ_BEFORE_SEND] = {.value = {.simple_preference = PROHIBIT}}, // Temporary - to make it easy to ban quic
      }
    },
    .init = quic_init,
    .send = quic_send,
    .init_with_send = quic_init_with_send,
    .listen = quic_listen,
    .stop_listen = quic_stop_listen,
    .close = quic_close,
    .abort = quic_abort,
    .clone_connection = quic_clone_connection,
    .remote_endpoint_from_peer = quic_remote_endpoint_from_peer,
    .retarget_protocol_connection = quic_retarget_protocol_connection
};

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);


ct_quic_group_state_t* ct_create_quic_group_state() {
  ct_quic_group_state_t* state = malloc(sizeof(ct_quic_group_state_t));
  if (state) {
    memset(state, 0, sizeof(ct_quic_group_state_t));
  }
  else {
    log_error("Failed to allocate memory for QUIC group state");
  }
  return state;
}

void ct_free_quic_group_state(ct_quic_group_state_t* state) {
  if (state) {
    free(state);
  }
}

void ct_free_quic_stream_state(ct_quic_stream_state_t* state) {
  if (state) {
    free(state);
  }
}

// Forward declaration for timer callback
void on_quic_context_timer(uv_timer_t* timer_handle);

static void quic_context_timer_close_cb(uv_handle_t* handle) {
  log_trace("Successfully closed QUIC context timer handle: %p", handle);
  ct_quic_context_t* quic_ctx = (ct_quic_context_t*)handle->data;
  if (quic_ctx) {
    int rc = picoquic_save_session_tickets(quic_ctx->picoquic_ctx, quic_ctx->ticket_store_path);
    if (rc != 0) {
      log_error("Failed to save QUIC session tickets to store %s: %d", quic_ctx->ticket_store_path, rc);
    } else {
      log_trace("Successfully saved QUIC session tickets to store %s", quic_ctx->ticket_store_path);
    }
  }
}

ct_quic_context_t* ct_create_quic_context(const char* cert_file,
                                          const char* key_file,
                                          struct ct_listener_s* listener,
                                          ct_connection_group_t* connection_group,
                                          const ct_security_parameters_t* security_parameters,
                                          ct_message_t* initial_message, // to be freed in case this connection suceeds
                                          ct_message_context_t* initial_message_context // to be freed in case this connection suceeds
                                          ) {
  if (!cert_file || !key_file || !security_parameters) {
    log_error("Certificate, key files and security parameters are required for QUIC context creation");
    return NULL;
  }

  const char* ticket_store_path = ct_sec_param_get_ticket_store_path(security_parameters);

  ct_quic_context_t* quic_ctx = malloc(sizeof(ct_quic_context_t));
  if (!quic_ctx) {
    log_error("Failed to allocate memory for QUIC context");
    return NULL;
  }
  memset(quic_ctx, 0, sizeof(ct_quic_context_t));

  quic_ctx->initial_message = initial_message;
  quic_ctx->initial_message_context = initial_message_context;

  // Store certificate file names (deep copy)
  quic_ctx->cert_file_name = strdup(cert_file);
  if (!quic_ctx->cert_file_name) {
    log_error("Failed to duplicate certificate file name");
    free(quic_ctx);
    return NULL;
  }

  quic_ctx->key_file_name = strdup(key_file);
  if (!quic_ctx->key_file_name) {
    log_error("Failed to duplicate key file name");
    free(quic_ctx->cert_file_name);
    free(quic_ctx);
    return NULL;
  }

  quic_ctx->listener = listener;
  quic_ctx->connection_group = connection_group;

  // Store ticket store path for 0-RTT session persistence
  if (ticket_store_path) {
    log_trace("Setting ticket store path to %s for QUIC context", ticket_store_path);
    quic_ctx->ticket_store_path = strdup(ticket_store_path);
    if (!quic_ctx->ticket_store_path) {
      log_error("Failed to duplicate ticket store path");
      free(quic_ctx->key_file_name);
      free(quic_ctx->cert_file_name);
      free(quic_ctx);
      return NULL;
    }
  } else {
    log_trace("Ticket store path not specified in security parameters for QUIC context");
    quic_ctx->ticket_store_path = NULL;
  }

  size_t out_num_alpns = 0;

  const char** alpn_strings = ct_sec_param_get_alpn_strings(security_parameters, &out_num_alpns);
  if (!alpn_strings) {
    log_error("No ALPN strings specified in security parameters for QUIC context");
    free(quic_ctx->ticket_store_path);
    free(quic_ctx->key_file_name);
    free(quic_ctx->cert_file_name);
    return NULL;
  }
  if (out_num_alpns == 0) {
    log_error("ALPN string array is empty in security parameters for QUIC context");
    free(quic_ctx->ticket_store_path);
    free(quic_ctx->key_file_name);
    free(quic_ctx->cert_file_name);
    return NULL;
  }

  uint8_t* ticket_key = NULL;
  size_t ticket_key_length = 0;

  const ct_byte_array_t* stek = ct_sec_param_get_session_ticket_encryption_key(security_parameters);
  log_info("Stek address: %p", (void*)stek);
  if (stek) {
    log_trace("Using session ticket encryption key of length %zu from security parameters", ticket_key_length);
    ticket_key = stek->bytes;
    ticket_key_length = stek->length;
  }

  // Create picoquic context
  quic_ctx->picoquic_ctx = picoquic_create(
      MAX_CONCURRENT_QUIC_CONNECTIONS,
      quic_ctx->cert_file_name,
      quic_ctx->key_file_name,
      NULL,
      alpn_strings[0],
      picoquic_callback,
      quic_ctx,   // Default callback context is the quic_context
      NULL,
      NULL,
      NULL,
      picoquic_current_time(),
      NULL,
      ticket_store_path,
      ticket_key,
      ticket_key_length
  );

  if (!quic_ctx->picoquic_ctx) {
    log_error("Failed to create picoquic context");
    free(quic_ctx->ticket_store_path);
    free(quic_ctx->key_file_name);
    free(quic_ctx->cert_file_name);
    free(quic_ctx);
    return NULL;
  }

  // Set up timer handle for this context
  quic_ctx->timer_handle = malloc(sizeof(uv_timer_t));
  if (!quic_ctx->timer_handle) {
    log_error("Failed to allocate memory for QUIC context timer");
    picoquic_free(quic_ctx->picoquic_ctx);
    free(quic_ctx->ticket_store_path);
    free(quic_ctx->key_file_name);
    free(quic_ctx->cert_file_name);
    free(quic_ctx);
    return NULL;
  }

  int rc = uv_timer_init(event_loop, quic_ctx->timer_handle);
  if (rc < 0) {
    log_error("Error initializing QUIC context timer: %s", uv_strerror(rc));
    free(quic_ctx->timer_handle);
    picoquic_free(quic_ctx->picoquic_ctx);
    free(quic_ctx->ticket_store_path);
    free(quic_ctx->key_file_name);
    free(quic_ctx->cert_file_name);
    free(quic_ctx);
    return NULL;
  }

  // Store context pointer in timer for access in callback
  quic_ctx->timer_handle->data = quic_ctx;

  log_debug("Created QUIC context with cert=%s, key=%s", cert_file, key_file);
  return quic_ctx;
}

void ct_close_quic_context(ct_quic_context_t* quic_ctx) {
  if (!quic_ctx) {
    return;
  }
  log_trace("Closing QUIC context");
  if (quic_ctx->timer_handle) {
    uv_timer_stop(quic_ctx->timer_handle);
    // Callback will handle freeing
    uv_close((uv_handle_t*)quic_ctx->timer_handle, quic_context_timer_close_cb);
    return;
  }
}

// Forward declarations of helper functions
bool ct_connection_stream_is_initialized(ct_connection_t* connection);
void ct_quic_set_connection_stream(ct_connection_t* connection, uint64_t stream_id);
void ct_connection_assign_next_free_stream(ct_connection_t* connection, bool is_unidirectional);
uint64_t ct_connection_get_stream_id(const ct_connection_t* connection);
ct_quic_stream_state_t* ct_connection_get_stream_state(const ct_connection_t* connection);
picoquic_cnx_t* ct_connection_get_picoquic_connection(const ct_connection_t* connection);

bool ct_connection_stream_is_initialized(ct_connection_t* connection) {
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    return false;
  }
  return stream_state->stream_initialized;
}

void ct_quic_set_connection_stream(ct_connection_t* connection, uint64_t stream_id) {
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    return;
  }
  log_debug("Setting QUIC stream ID %llu for connection %s", (unsigned long long)stream_id, connection->uuid);
  stream_state->stream_id = stream_id;
  stream_state->stream_initialized = true;
}

void ct_connection_assign_next_free_stream(ct_connection_t* connection, bool is_unidirectional) {
  ct_quic_group_state_t* group_state = connection->connection_group->connection_group_state;
  picoquic_cnx_t* cnx = group_state->picoquic_connection;

  uint64_t next_stream_id = picoquic_get_next_local_stream_id(cnx, is_unidirectional);
  log_debug("Assigned QUIC stream ID: %llu (unidirectional: %d)", (unsigned long long)next_stream_id, is_unidirectional);

  log_debug("Assigning next free stream ID, which is: ID %llu to connection %s", (unsigned long long)next_stream_id, connection->uuid);
  ct_quic_set_connection_stream(connection, next_stream_id);
  picoquic_set_app_stream_ctx(cnx, next_stream_id, connection);
}

uint64_t ct_connection_get_stream_id(const ct_connection_t* connection) {
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    return 0;
  }
  return stream_state->stream_id;
}

ct_quic_group_state_t* ct_connection_get_quic_group_state(const ct_connection_t* connection) {
  if (!connection || !connection->connection_group || !connection->connection_group->connection_group_state) {
    log_error("Cannot get QUIC group state, connection or group state is NULL");
    log_debug("conn=%p, group=%p, group_state=%p", 
              (void*)connection,
              (void*)(connection ? connection->connection_group : NULL), 
              (void*)(connection && connection->connection_group ? connection->connection_group->connection_group_state : NULL));
    return NULL;
  }
  return (ct_quic_group_state_t*)connection->connection_group->connection_group_state;
}

ct_quic_stream_state_t* ct_connection_get_stream_state(const ct_connection_t* connection) {
  if (!connection || !connection->internal_connection_state) {
    log_error("Cannot get stream state, connection or internal state is NULL");
    return NULL;
  }
  return (ct_quic_stream_state_t*)connection->internal_connection_state;
}

picoquic_cnx_t* ct_connection_get_picoquic_connection(const ct_connection_t* connection) {
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  if (!group_state) {
    log_error("Cannot get picoquic connection, group state is NULL");
    return NULL;
  }
  return group_state->picoquic_connection;
}

ct_quic_context_t* ct_connection_get_quic_context(const ct_connection_t* connection) {
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  if (!group_state) {
    log_error("Cannot get QUIC context, group state is NULL");
    return NULL;
  }
  return group_state->quic_context;
}


size_t quic_alpn_select_cb(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count) {
  log_trace("QUIC server alpn select cb");

  (void)count;

  // Get the QUIC context from the default callback context
  // The quic_context stores the listener pointer
  ct_quic_context_t* quic_context = picoquic_get_default_callback_context(quic);
  if (!quic_context || !quic_context->listener) {
    log_error("ALPN select callback: no listener associated with QUIC context");
    return count;  // Return count to indicate no match
  }

  ct_listener_t* listener = quic_context->listener;

  if (!listener->security_parameters->security_parameters[ALPN].value.array_of_strings) {
    log_warn("Listener has no ALPNs configured for selection");
    return count;
  }

  const ct_string_array_value_t* listener_alpns = listener->security_parameters->security_parameters[ALPN].value.array_of_strings;

  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < listener_alpns->num_strings; j++) {
      if (strcmp((const char*)list[i].base, listener_alpns->strings[j]) == 0) {
        log_trace("Selected ALPN: %.*s", (int)list[i].len, list[i].base);
        return i;
      }
    }
  }
  log_warn("No compatible ALPN found for attempted connection to listener");
  return count;
}

void reset_quic_timer(ct_quic_context_t* quic_context) {
  if (!quic_context || !quic_context->picoquic_ctx || !quic_context->timer_handle) {
    log_error("Cannot reset QUIC timer: invalid context");
    log_debug("ctx=%p, ctx->quic_ctx=%p, ctx->timer_handle=%p", (void*)quic_context, (void*)(quic_context ? quic_context->picoquic_ctx : NULL), (void*)(quic_context ? quic_context->timer_handle : NULL));
    return;
  }
  uint64_t next_wake_delay = picoquic_get_next_wake_delay(quic_context->picoquic_ctx, picoquic_get_quic_time(quic_context->picoquic_ctx), INT64_MAX - 1);
  log_debug("Resetting QUIC timer to fire in %llu ms", (unsigned long long)MICRO_TO_MILLI(next_wake_delay));
  uv_timer_start(quic_context->timer_handle, on_quic_context_timer, MICRO_TO_MILLI(next_wake_delay), 0);
}

void quic_closed_udp_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle for QUIC connection");
  ct_quic_context_t* quic_ctx = (ct_quic_context_t*)handle->data;
  ct_connection_group_t* group = quic_ctx->connection_group;

  gpointer key, value;
  GHashTableIter iter;
  if (group) {
    g_hash_table_iter_init(&iter, group->connections);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      ct_connection_t* conn = (ct_connection_t*)value;
      if (!ct_connection_is_closed(conn)) {
        ct_connection_mark_as_closed(conn);
        if (conn->connection_callbacks.closed) {
          log_trace("Invoking connection closed callback for connection: %s", conn->uuid);
          conn->connection_callbacks.closed(conn);
        }
        else {
          log_trace("No connection closed callback set for connection: %s", conn->uuid);
        }
      }
    }
  }
}

int handle_closed_picoquic_connection(ct_connection_t* connection) {
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  if (!group_state) {
    log_error("Cannot handle closed QUIC connection due to invalid parameter");
    return -EINVAL;
  }
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    log_error("Cannot handle closed QUIC connection, due to invalid parameter");
    return -EINVAL;
  }

  int rc = 0;
  if (connection->socket_type == CONNECTION_SOCKET_TYPE_STANDALONE) {
    log_info("Closing standalone QUIC connection with UDP handle: %p", group_state->udp_handle);

    rc = uv_udp_recv_stop(group_state->udp_handle);
    if (rc < 0) {
      log_error("Error closing underlying QUIC handles: %d", rc);
      return rc;
    }
    log_info("Closing UDP handle for standalone QUIC connection");
    uv_close((uv_handle_t*)group_state->udp_handle, quic_closed_udp_handle_cb);
    ct_close_quic_context(group_state->quic_context);
  }
  else if (connection->socket_type == CONNECTION_SOCKET_TYPE_MULTIPLEXED) {
    log_info("Removing closed QUIC connection group from socket manager");
    // The connection group's active count is already 0 at this point
    rc = socket_manager_remove_connection_group(
        connection->socket_manager,
        &connection->remote_endpoint.data.resolved_address);

    if (rc < 0) {
      log_error("Error removing connection group from socket manager: %d", rc);
      return rc;
    }
    log_info("Successfully removed connection group from socket manager");
  }
  else {
    log_error("Unknown connection open type when handling closed QUIC connection");
    return -EINVAL;
  }
  return 0;
}


/**
 * Helper function to process received stream data and deliver it to the application.
 *
 * @param connection The CTaps connection object
 * @param bytes Pointer to received data
 * @param length Length of received data in bytes
 * @return 0 on success, negative error code on failure
 */
static int handle_stream_data(ct_connection_t* connection, const uint8_t* bytes, size_t length) {
  if (length == 0) {
    log_trace("Received empty data chunk, nothing to process");
    return 0;
  }

  if (!connection) {
    log_error("Cannot handle stream data: connection is NULL");
    return -EINVAL;
  }

  // Check if connection can still receive data
  if (!ct_connection_can_receive(connection)) {
    log_error("Received data on stream after FIN was already received for connection %s", connection->uuid);
    return -EPIPE;
  }

  log_debug("Processing %zu bytes of received data for connection %s", length, connection->uuid);

  // Delegate to connection receive handler (handles framing and application delivery)
  ct_connection_on_protocol_receive(connection, bytes, length);

  return 0;
}

/**
 * Helper function to handle FIN reception on a stream.
 * Sets canReceive=false and closes the connection if both directions are closed.
 *
 * @param connection The CTaps connection object
 */
static void handle_stream_fin(ct_connection_t* connection) {
  if (!connection) {
    log_error("Cannot handle stream FIN: connection is NULL");
    return;
  }

  log_info("Handling FIN for connection %s", connection->uuid);

  // RFC 9622: Set canReceive to false when Final message received
  ct_connection_set_can_receive(connection, false);

  // Check if both send and receive directions are closed
  bool can_send = ct_connection_can_send(connection);

  if (!can_send) {
    // Both directions closed - close the connection per our earlier decision
    log_info("Both send and receive sides closed for connection %s, closing connection", connection->uuid);
    ct_connection_close(connection);
  } else {
    log_debug("FIN received but send direction still open for connection %s (half-close)", connection->uuid);
  }
}

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
  (void)cnx;
  ct_connection_group_t* connection_group = (ct_connection_group_t*)callback_ctx;
  ct_connection_t* connection = NULL;
  log_trace("ct_callback_t event with connection group: %s", connection_group->connection_group_id);
  log_trace("Received callback event: %d", fin_or_event);

  if (!connection_group) {
    log_error("Connection group is NULL in picoquic callback");
    return -EINVAL;
  }

  ct_quic_group_state_t* group_state = (ct_quic_group_state_t*)connection_group->connection_group_state;
  switch (fin_or_event) {
    case picoquic_callback_ready:
      log_debug("QUIC connection is ready, invoking CTaps callback");
      // the picoquic_callback_ready event is per-cnx.
      // This means that this callback only happens once per connection group.
      // We therefore know that the connection group only has one connection at this point.
      connection = ct_connection_group_get_first(connection_group);
      if (!connection) {
        log_error("No connections found in connection group during ready callback");
        return -EINVAL;
      }

      ct_quic_context_t* quic_context = ct_connection_get_quic_context(connection);
      if (quic_context->initial_message) {
        ct_message_free(quic_context->initial_message);
        quic_context->initial_message = NULL;
      }
      if (quic_context->initial_message_context) {
        ct_message_context_free(quic_context->initial_message_context);
        quic_context->initial_message_context = NULL;
      }

      if (ct_connection_is_server(connection)) {
        log_debug("Server connection ready, notifying listener");
        ct_listener_t* listener = connection->socket_manager->listener;

        int rc = resolve_local_endpoint_from_handle((uv_handle_t*)group_state->udp_handle, connection);
        if (rc < 0) {
          log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
        }
        ct_connection_mark_as_established(connection);
        if (listener->listener_callbacks.connection_received) {
          log_debug("Invoking listener connection received callback for new server connection");
          listener->listener_callbacks.connection_received(listener, connection);
        }
        else {
          log_warn("No connection received callback set on listener");
        }
      }
      else if (ct_connection_is_client(connection)) {
        if (picoquic_tls_is_psk_handshake(group_state->picoquic_connection)) {
          log_trace("Client connection was established with 0-RTT");
          ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
          if (stream_state->attempted_early_data) {
            log_trace("Client connection sent early data together with 0-RTT");
            ct_connection_set_sent_early_data(connection, true);
          }
          else {
            log_trace("Client connection did not send early data with 0-RTT");
          }
        }
        else {
          log_trace("Client connection did not use 0-RTT");
        }
        log_debug("Client connection ready, notifying application");
        ct_connection_mark_as_established(connection);
        if (connection->connection_callbacks.ready) {
          connection->connection_callbacks.ready(connection);
        }
      }
      else {
        log_error("Unknown connection role in picoquic ready callback");
      }
      break;
    case picoquic_callback_stream_data:
      log_debug("Received %zu bytes on stream %llu", length, (unsigned long long)stream_id);

      // Check if this is a new stream (stream context is NULL)
      if (v_stream_ctx == NULL) {
        log_debug("Received data on new stream %llu from remote", (unsigned long long)stream_id);

        // Get the first connection
        ct_connection_t* first_connection = ct_connection_group_get_first(connection_group);
        if (!first_connection) {
          log_error("No connections in group when receiving new stream");
          return -EINVAL;
        }

        // If we have received a new stream, but the first connection already has a stream initialized,
        if (ct_connection_stream_is_initialized(first_connection)) {
          log_info("Received stream id is: %llu", (unsigned long long)stream_id);
          log_info("first connection stream id is: %llu", (unsigned long long)ct_connection_get_stream_id(first_connection));
          if (ct_connection_is_server(first_connection)) {
            log_info("Received new remote-initiated stream on server connection");

            // Create new connection for this stream by cloning the first connection
            ct_connection_t* new_stream_connection = ct_connection_create_clone(first_connection);
            if (!new_stream_connection) {
              log_error("Failed to create cloned connection for new stream");
              return -ENOMEM;
            }

            // Set the stream ID on the cloned connection so responses go to the correct stream
            ct_quic_set_connection_stream(new_stream_connection, stream_id);

            int rc = picoquic_set_app_stream_ctx(group_state->picoquic_connection, stream_id, new_stream_connection);
            if (rc < 0) {
              log_error("Failed to set stream context for new stream connection: %d", rc);
              return rc;
            }

            ct_listener_t* listener = first_connection->socket_manager->listener;
            if (listener) {
              ct_connection_mark_as_established(new_stream_connection);

              int rc = resolve_local_endpoint_from_handle((uv_handle_t*)group_state->udp_handle, new_stream_connection);
              if (rc < 0) {
                log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
              }
              listener->listener_callbacks.connection_received(listener, new_stream_connection);
            } else {
              log_warn("Received new stream but listener has been closed, not notifying application");
            }

            // Use helper function to handle received data
            return handle_stream_data(new_stream_connection, bytes, length);
          } if (ct_connection_is_client(first_connection)) {
            log_error("Received new remote-initiated stream on client connection - multi-streaming not yet implemented");
            log_info("Stream id is: %d", stream_id);
            return -ENOSYS;
          } else {
            log_error("Unknown connection role when handling new remote-initiated stream");
            return -EINVAL;
          }
        } else {
          log_debug("First connection has uninitialized stream, using it for stream %llu", (unsigned long long)stream_id);
          picoquic_state_enum curr_picoquic_state = picoquic_get_cnx_state(group_state->picoquic_connection);
          if (curr_picoquic_state < picoquic_state_ready && curr_picoquic_state >= picoquic_state_server_init) {
            log_debug("Picoquic received data in early state: %d", curr_picoquic_state);
          }

          ct_quic_set_connection_stream(first_connection, stream_id);

          picoquic_set_app_stream_ctx(group_state->picoquic_connection, stream_id, first_connection);

          // Use helper function to handle received data
          return handle_stream_data(first_connection, bytes, length);
        }
      } else {
        // Existing stream - get connection from stream context
        connection = (ct_connection_t*)v_stream_ctx;
        log_trace("Got connection %s from stream context for stream %llu", connection->uuid, (unsigned long long)stream_id);
        // Use helper function to handle received data
        return handle_stream_data(connection, bytes, length);
      }
      break;
    case picoquic_callback_stream_fin:
      log_info("Received FIN on stream %llu, with data length: %zu", (unsigned long long)stream_id, length);

      if (v_stream_ctx) {
        connection = (ct_connection_t*)v_stream_ctx;

        // Handle any data that came with the FIN first
        if (length > 0) {
          log_debug("FIN received with %zu bytes of data, processing data first", length);
          int ret = handle_stream_data(connection, bytes, length);
          if (ret != 0) {
            log_error("Error handling data received with FIN: %d", ret);
            return ret;
          }
        }

        // Now handle the FIN itself
        handle_stream_fin(connection);
      } else {
        log_warn("Received FIN on stream %llu but no stream context available", (unsigned long long)stream_id);
      }
      break;
    case picoquic_callback_stream_reset:
      log_info("Received RESET on stream %llu", (unsigned long long)stream_id);
      if (v_stream_ctx) {
        connection = (ct_connection_t*)v_stream_ctx;
        log_info("Peer reset stream for connection %p", (void*)connection);

        if (!ct_connection_is_closed_or_closing(connection)) {
          ct_connection_group_decrement_active(connection_group);
          ct_connection_mark_as_closed(connection);
        }
      } else {
        log_warn("Received RESET on stream %llu but no stream context available", (unsigned long long)stream_id);
      }
      break;
    case picoquic_callback_close:
    case picoquic_callback_application_close:
      {
        if (fin_or_event == picoquic_callback_close) {
          log_info("Picoquic connection closed by peer");
        } else {
          log_info("Picoquic connection application-closed by peer");
        }

        // Reset the active connection counter since entire QUIC connection is closed
        connection_group->num_active_connections = 0;

        // Get first connection to determine type and handle cleanup
        connection = ct_connection_group_get_first(connection_group);
        if (connection) {
          log_info("Picoquic close callback for connection: %p", (void*)connection);
          handle_closed_picoquic_connection(connection);
        }
      }
      break;
    case picoquic_callback_request_alpn_list:
      log_warn("ALPN list requested in callback, should never happen");
      return -EINVAL;
      break;
    default:
      log_debug("Unhandled callback event: %d", fin_or_event);
      break;
  }
  return 0;
}

static void alloc_quic_buf(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
	(void)handle;
	*buf = uv_buf_init(malloc(size), size);
}

void on_quic_udp_send(uv_udp_send_t* req, int status) {
  log_debug("Sent QUIC packet over UDP");
  if (status) {
    log_error("Send error: %s\n", uv_strerror(status));
  }
  if (req) {
    // Free the buffer data that was allocated for the async send
    if (req->data) {
      uv_buf_t* buf = (uv_buf_t*)req->data;
      if (buf->base) {
        free(buf->base);
      }
      free(buf);
    }
    free(req);  // Free the send request
  }
}

void on_quic_udp_read(uv_udp_t* udp_handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr_from, unsigned flags) {
  (void)flags;
  log_debug("Received QUIC message over UDP");
  if (nread < 0) {
    log_error("Read error: %s\n", uv_strerror(nread));
    uv_close((uv_handle_t*)udp_handle, NULL);
    free(buf->base);
    return;
  }

  if (addr_from == NULL && nread == 0) {
    // No more data to read
    if (buf->base) {
      free(buf->base);
    }
    return;
  }

  ct_quic_context_t* quic_context = (ct_quic_context_t*)udp_handle->data;
  if (!quic_context || !quic_context->picoquic_ctx) {
    log_error("No QUIC context associated with UDP handle");
    free(buf->base);
    return;
  }

  picoquic_quic_t* picoquic_ctx = quic_context->picoquic_ctx;
  picoquic_cnx_t *cnx = NULL;

  struct sockaddr_storage addr_to_storage;
  int namelen = sizeof(struct sockaddr_storage);
  int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)&addr_to_storage, &namelen);
  if (rc < 0) {
    log_error("Error getting UDP socket name for incoming QUIC packet: %s", uv_strerror(rc));
    free(buf->base);
    return;
  }

  rc = picoquic_incoming_packet_ex(
    picoquic_ctx,
    (uint8_t*)buf->base,
    nread,
    (struct sockaddr*)addr_from,
    (struct sockaddr*)&addr_to_storage,
    0,
    0,
    &cnx,
    picoquic_get_quic_time(picoquic_ctx)
  );
  free(buf->base);
  if (rc != 0) {
    log_error("Error processing incoming QUIC packet: %d", rc);
    // TODO - error handling
  }

  // If we haven't set the callback context, this means this cnx was just created by picoquic, need to
  // create our own ct_connection_t
  if (picoquic_get_callback_context(cnx) == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
    log_info("Received packet for new QUIC cnx for listener");
    ct_listener_t* listener = quic_context->listener;

    if (!listener) {
      log_error("No listener associated with QUIC context for incoming connection");
      return;
    }

    if (rc != 0) {
      log_error("Could not get remote address from picoquic connection: %d", rc);
      return;
    }

    ct_connection_group_t* connection_group = socket_manager_get_or_create_connection_group(
        listener->socket_manager,
        (struct sockaddr_storage*)addr_from,
        NULL);

    if (!connection_group) {
      log_error("Failed to get or create connection group for new QUIC connection");
      return;
    }

    // Get the first (and only) connection in the newly created group
    ct_connection_t* connection = ct_connection_group_get_first(connection_group);
    if (!connection) {
      log_error("Connection group exists but has no connections");
      return;
    }

    log_trace("Created new ct_connection_t object for received QUIC cnx: %p", (void*)connection);

    // Set picoquic callback to connection group (not individual connection)
    picoquic_set_callback(cnx, picoquic_callback, connection->connection_group);

    // Allocate shared group state for this connection
    ct_quic_group_state_t* group_state = ct_create_quic_group_state();
    if (!group_state) {
      log_error("Failed to allocate memory for QUIC group state");
      free(connection);
      return;
    }
    group_state->picoquic_connection = cnx;
    group_state->quic_context = quic_context;  // Share the listener's quic_context

    log_trace("Setting up received ct_connection_t state for new ct_connection_t");
    ct_quic_group_state_t* listener_group_state = (ct_quic_group_state_t*)listener->socket_manager->internal_socket_manager_state;
    group_state->udp_handle = listener_group_state->udp_handle;
    rc = resolve_local_endpoint_from_handle((uv_handle_t*)group_state->udp_handle, connection);
    if (rc < 0) {
      log_error("Could not get UDP socket name for QUIC connection: %s", uv_strerror(rc));
      free(group_state);
      free(connection);
      return;
    }

    connection->connection_group->connection_group_state = group_state;

    // Allocate per-stream state (stream_id will be set when stream is created)
    ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
    if (!stream_state) {
      log_error("Failed to allocate memory for QUIC stream state");
      free(group_state);
      free(connection);
      return;
    }
    stream_state->stream_id = 0;
    stream_state->stream_initialized = false;
    connection->internal_connection_state = stream_state;
    log_trace("Done setting up received QUIC connection state");
  }

  log_trace("Processed incoming QUIC packet, picoquic connection: %p", (void*)cnx);

  reset_quic_timer(quic_context);
}

void on_quic_context_timer(uv_timer_t* timer_handle) {
  ct_quic_context_t* quic_ctx = (ct_quic_context_t*)timer_handle->data;
  if (!quic_ctx || !quic_ctx->picoquic_ctx) {
    log_error("QUIC context timer triggered but context is invalid");
    return;
  }

  log_trace("QUIC context timer triggered, checking for new QUIC packets to send");

  picoquic_quic_t* picoquic_ctx = quic_ctx->picoquic_ctx;
  size_t send_length = 0;

  struct sockaddr_storage from_address;
  struct sockaddr_storage to_address;
  int if_index = -1;
  picoquic_cnx_t* last_cnx = NULL;

  do {
    send_length = 0;

    // Allocate buffer on heap for each packet (freed in on_quic_udp_send callback)
    unsigned char* send_buffer_base = malloc(MAX_QUIC_PACKET_SIZE);
    if (!send_buffer_base) {
      log_error("Failed to allocate buffer for QUIC packet");
      break;
    }

    int rc = picoquic_prepare_next_packet(
      picoquic_ctx,
      picoquic_get_quic_time(picoquic_ctx),
      send_buffer_base,
      MAX_QUIC_PACKET_SIZE,
      &send_length,
      &to_address,
      &from_address,
      &if_index,
      NULL,
      &last_cnx
    );
    if (rc != 0) {
      log_error("Error preparing next QUIC packet: %d", rc);
      free(send_buffer_base);
      break;
    }

    log_debug("Prepared QUIC packet of length %zu", send_length);
    if (send_length > 0) {

      ct_connection_group_t* connection_group = (ct_connection_group_t*)picoquic_get_callback_context(last_cnx);
      ct_quic_group_state_t* group_state = (ct_quic_group_state_t*)connection_group->connection_group_state;

      uv_udp_t* udp_handle = group_state->udp_handle;

      // Allocate uv_buf_t structure on heap
      uv_buf_t* send_buffer = malloc(sizeof(uv_buf_t));
      if (!send_buffer) {
        log_error("Failed to allocate uv_buf_t for QUIC packet");
        free(send_buffer_base);
        break;
      }
      *send_buffer = uv_buf_init((char*)send_buffer_base, send_length);

      uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
      if (!send_req) {
        log_error("Failed to allocate send request for QUIC packet");
        free(send_buffer_base);
        free(send_buffer);
        break;
      }

      // Store buffer in send_req->data so callback can free it
      send_req->data = send_buffer;

      log_trace("Sending QUIC data over UDP handle");
      rc = uv_udp_send(
        send_req,
        udp_handle,
        send_buffer,
        1,
        (struct sockaddr*)&to_address,
        on_quic_udp_send);
      if (rc < 0) {
        log_error("Error sending QUIC packet over UDP: %s", uv_strerror(rc));
        free(send_buffer_base);
        free(send_buffer);
        free(send_req);
        break;
      }
      log_debug("Sent QUIC packet of length %zu", send_length);
    } else {
      log_trace("No QUIC data to send at this time");
      // No data to send, free the buffer
      free(send_buffer_base);
    }
  } while (send_length > 0);
  log_debug("Finished sending QUIC packets");

  reset_quic_timer(quic_ctx);
}

// TODO - this and quic_init shares a lot of code, should refactor to common function
int quic_init_with_send(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context) {
  (void)connection_callbacks;
  (void)initial_message_context;
  log_info("Initializing standalone QUIC connection and attempting early data");

  // Get certificate from security parameters
  if (!connection->security_parameters) {
    log_error("Security parameters required for QUIC connection");
    return -EINVAL;
  }

  const ct_certificate_bundles_t* cert_bundles =
      connection->security_parameters->security_parameters[CLIENT_CERTIFICATE].value.certificate_bundles;

  if (!cert_bundles || cert_bundles->num_bundles == 0) {
    log_error("No certificate bundle configured for QUIC client connection");
    return -EINVAL;
  }

  const char* cert_file = cert_bundles->certificate_bundles[0].certificate_file_name;
  const char* key_file = cert_bundles->certificate_bundles[0].private_key_file_name;

  if (!cert_file || !key_file) {
    log_error("Certificate or key file not configured in security parameters");
    log_debug("cert_file=%p, key_file=%p", (void*)cert_file, (void*)key_file);
    return -EINVAL;
  }

  ct_quic_context_t* quic_context = ct_create_quic_context(
    cert_file,
    key_file,
    NULL,
    connection->connection_group,
    connection->security_parameters,
    initial_message,
    initial_message_context
  );

  if (!quic_context) {
    log_error("Failed to create QUIC context for client connection");
    return -EIO;
  }

  uint64_t current_time = picoquic_get_quic_time(quic_context->picoquic_ctx);

  uv_udp_t* udp_handle = create_udp_listening_on_local(&connection->local_endpoint, alloc_quic_buf, on_quic_udp_read);
  if (!udp_handle) {
    log_error("Failed to create UDP handle for QUIC connection");
    ct_close_quic_context(quic_context);
    return -EIO;
  }

  // Store quic_context in udp_handle->data for access in on_quic_udp_read
  udp_handle->data = quic_context;
  log_debug("Created UDP handle %p for QUIC connection", (void*)udp_handle);


  // Allocate shared group state (UDP handle + QUIC connection)
  ct_quic_group_state_t* group_state = ct_create_quic_group_state();
  if (!group_state) {
    log_error("Failed to allocate QUIC group state");
    ct_close_quic_context(quic_context);
    return -ENOMEM;
  }


  *group_state = (ct_quic_group_state_t){
    .udp_handle = udp_handle,
    .picoquic_connection = NULL,
    .quic_context = quic_context,
  };
  connection->connection_group->connection_group_state = group_state;

  ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
  if (!stream_state) {
    log_error("Failed to allocate QUIC stream state");
    free(group_state);
    ct_close_quic_context(quic_context);
    return -ENOMEM;
  }
  stream_state->stream_id = 0;
  stream_state->stream_initialized = false;
  connection->internal_connection_state = stream_state;

  int rc = resolve_local_endpoint_from_handle((uv_handle_t*)udp_handle, connection);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    free(group_state);
    free(stream_state);
    ct_close_quic_context(quic_context);
    return rc;
  }

  size_t alpn_count = 0;
  const char** alpn_strings = ct_sec_param_get_alpn_strings(connection->security_parameters, &alpn_count);
  if (alpn_count == 0) {
    log_error("No ALPN strings configured for QUIC connection");
    free(group_state);
    free(stream_state);
    ct_close_quic_context(quic_context);
    return -EINVAL;
  }

  group_state->picoquic_connection = picoquic_create_cnx(
      quic_context->picoquic_ctx,
      picoquic_null_connection_id,
      picoquic_null_connection_id,
      (struct sockaddr*) &connection->remote_endpoint.data.resolved_address,
      current_time,
      1,
      "localhost",
      alpn_strings[0], // We create separate candidates for each ALPN to support 0-rtt (see candidate gathering code)
      1
  );

  log_trace("Setting callback context to connection group: %p", (void*)connection->connection_group);

  // Set picoquic callback to connection group
  picoquic_set_callback(group_state->picoquic_connection, picoquic_callback, connection->connection_group);

  bool is_final = initial_message_context && ct_message_properties_get_final(ct_message_context_get_message_properties(initial_message_context));

  ct_connection_assign_next_free_stream(connection, false);
  rc = picoquic_add_to_stream(
      group_state->picoquic_connection,
      ct_connection_get_stream_id(connection),
      (const uint8_t*)initial_message->content,
      initial_message->length,
      is_final
  );

  picoquic_set_app_stream_ctx(group_state->picoquic_connection, ct_connection_get_stream_id(connection), connection);
  if (rc < 0) {
    log_error("Failed to add initial message to QUIC stream: %d", rc);
    return rc;
  }
  stream_state->attempted_early_data = true;

  rc = picoquic_start_client_cnx(group_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    free(group_state);
    free(stream_state);
    ct_close_quic_context(quic_context);
    return rc;
  }


  reset_quic_timer(quic_context);
  log_trace("Successfully initiated standalone QUIC connection %p", (void*)connection);
  return 0;
}

int quic_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks) {
  (void)connection_callbacks;
  log_info("Initializing standalone QUIC connection");

  // Get certificate from security parameters
  if (!connection->security_parameters) {
    log_error("Security parameters required for QUIC connection");
    return -EINVAL;
  }

  const ct_certificate_bundles_t* cert_bundles =
      connection->security_parameters->security_parameters[CLIENT_CERTIFICATE].value.certificate_bundles;

  if (!cert_bundles || cert_bundles->num_bundles == 0) {
    log_error("No certificate bundle configured for QUIC client connection");
    return -EINVAL;
  }

  const char* cert_file = cert_bundles->certificate_bundles[0].certificate_file_name;
  const char* key_file = cert_bundles->certificate_bundles[0].private_key_file_name;

  if (!cert_file || !key_file) {
    log_error("Certificate or key file not configured in security parameters");
    return -EINVAL;
  }

  ct_quic_context_t* quic_context = ct_create_quic_context(
    cert_file,
    key_file,
    NULL,
    connection->connection_group,
    connection->security_parameters,
    NULL,
    NULL
  );

  if (!quic_context) {
    log_error("Failed to create QUIC context for client connection");
    return -EIO;
  }

  uint64_t current_time = picoquic_get_quic_time(quic_context->picoquic_ctx);

  uv_udp_t* udp_handle = create_udp_listening_on_local(&connection->local_endpoint, alloc_quic_buf, on_quic_udp_read);
  if (!udp_handle) {
    log_error("Failed to create UDP handle for QUIC connection");
    ct_close_quic_context(quic_context);
    return -EIO;
  }

  // Store quic_context in udp_handle->data for access in on_quic_udp_read
  udp_handle->data = quic_context;
  log_debug("Created UDP handle %p for QUIC connection", (void*)udp_handle);


  // Allocate shared group state (UDP handle + QUIC connection)
  ct_quic_group_state_t* group_state = ct_create_quic_group_state();
  if (!group_state) {
    log_error("Failed to allocate QUIC group state");
    ct_close_quic_context(quic_context);
    return -ENOMEM;
  }

  *group_state = (ct_quic_group_state_t){
    .udp_handle = udp_handle,
    .picoquic_connection = NULL,
    .quic_context = quic_context,
  };
  if (!connection->connection_group) {
    log_error("Connection has no connection group assigned");
    free(group_state);
    ct_close_quic_context(quic_context);
    return -EINVAL;
  }
  connection->connection_group->connection_group_state = group_state;

  ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
  if (!stream_state) {
    log_error("Failed to allocate QUIC stream state");
    free(group_state);
    ct_close_quic_context(quic_context);
    return -ENOMEM;
  }
  stream_state->stream_id = 0;
  stream_state->stream_initialized = false;
  stream_state->attempted_early_data = false;
  connection->internal_connection_state = stream_state;

  int rc = resolve_local_endpoint_from_handle((uv_handle_t*)udp_handle, connection);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    free(group_state);
    free(stream_state);
    ct_close_quic_context(quic_context);
    return rc;
  }

  size_t alpn_count = 0;
  const char** alpn_strings = ct_sec_param_get_alpn_strings(connection->security_parameters, &alpn_count);
  if (alpn_count == 0) {
    log_error("No ALPN strings configured for QUIC connection");
    free(group_state);
    free(stream_state);
    ct_close_quic_context(quic_context);
    return -EINVAL;
  }

  group_state->picoquic_connection = picoquic_create_cnx(
      quic_context->picoquic_ctx,
      picoquic_null_connection_id,
      picoquic_null_connection_id,
      (struct sockaddr*) &connection->remote_endpoint.data.resolved_address,
      current_time,
      1,
      "localhost",
      alpn_strings[0], // We create separate candidates for each ALPN to support 0-rtt (see candidate gathering code)
      1
  );

  log_trace("Setting callback context to connection group: %p", (void*)connection->connection_group);

  // Set picoquic callback to connection group
  picoquic_set_callback(group_state->picoquic_connection, picoquic_callback, connection->connection_group);

  rc = picoquic_start_client_cnx(group_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    free(group_state);
    free(stream_state);
    ct_close_quic_context(quic_context);
    return rc;
  }


  reset_quic_timer(quic_context);
  log_trace("Successfully initiated standalone QUIC connection %p", (void*)connection);
  return 0;
}

int quic_close(ct_connection_t* connection) {
  int rc = 0;
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  uint64_t stream_id = ct_connection_get_stream_id(connection);
  ct_connection_group_t* connection_group = connection->connection_group;
  uint64_t num_active_connections = ct_connection_group_get_num_active_connections(connection_group);

  log_info("Closing QUIC connection, active connections in group: %u", num_active_connections);

  // Check if there are multiple active connections in the group
  if (num_active_connections > 1) {
    // Multiple streams active - only close this stream with FIN
    log_info("Multiple active connections in group, closing stream %llu with FIN",
             (unsigned long long)stream_id);

    if (ct_connection_stream_is_initialized(connection)) {
      // Send FIN on this stream to gracefully close it
      if (ct_connection_can_send(connection)) {
        log_debug("Sending FIN on stream %llu for connection: %s", (unsigned long long)stream_id, connection->uuid);
        rc = picoquic_add_to_stream(group_state->picoquic_connection, stream_id, NULL, 0, 1);
        if (rc != 0) {
          log_error("Error sending FIN on stream %llu: %d", (unsigned long long)stream_id, rc);
        }
      }
    }

    // Decrement active connection counter and mark as closed
    ct_connection_group_decrement_active(connection_group);
    ct_connection_mark_as_closed(connection);
  } else {
    log_info("Last active connection in group, closing entire QUIC connection");
    rc = picoquic_close(group_state->picoquic_connection, 0);
    if (rc != 0) {
      log_error("Error closing picoquic connection: %d", rc);
    }

    ct_connection_group_decrement_active(connection_group);
  }

  reset_quic_timer(group_state->quic_context);
  return rc;
}

void quic_abort(ct_connection_t* connection) {
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  uint64_t stream_id = ct_connection_get_stream_id(connection);
  ct_connection_group_t* connection_group = connection->connection_group;
  uint64_t num_active_connections = ct_connection_group_get_num_active_connections(connection_group);

  log_info("Aborting connection using QUIC, active connections in group: %u", num_active_connections);

  // Check if there are multiple active connections in the group
  if (num_active_connections > 1) {
    // Multiple streams active - force close this stream with RST 
    log_info("Multiple active connections in group, closing stream %llu with RST",
             (unsigned long long)stream_id);

    if (ct_connection_stream_is_initialized(connection)) {
      log_debug("Sending RST on stream %llu for connection: %s", (unsigned long long)stream_id, connection->uuid);
      int rc = picoquic_reset_stream(group_state->picoquic_connection, stream_id, 0);
      if (rc != 0) {
        log_error("Error sending RST on stream %llu: %d", (unsigned long long)stream_id, rc);
      }
    }
    else {
      log_debug("Stream %llu not initialized, no RST sent", (unsigned long long)stream_id);
    }

    // Decrement active connection counter and mark as closed
    ct_connection_group_decrement_active(connection_group);
    ct_connection_mark_as_closed(connection);
  } else {
    log_info("Last active connection in group, closing entire QUIC connection");
    // Marking as closed etc. is handled in callback
    picoquic_close_immediate(group_state->picoquic_connection);
  }

  reset_quic_timer(group_state->quic_context);
}

int quic_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection) {
  (void)source_connection;
  log_debug("Creating clone of QUIC connection using multistreaming");
  target_connection->internal_connection_state = malloc(sizeof(ct_quic_stream_state_t));
  ct_quic_stream_state_t* target_state = (target_connection->internal_connection_state);
  target_state->stream_id = 0;
  target_state->stream_initialized = false;
  ct_connection_mark_as_established(target_connection);

  log_trace("QUIC cloned connection ready: %s", target_connection->uuid);
  // call ready callback of target connection
  if (target_connection->connection_callbacks.ready) {
    target_connection->connection_callbacks.ready(target_connection);
  }
  return 0;
}


int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* ctx) {
  log_debug("Sending message over QUIC");
  picoquic_cnx_t* cnx = ct_connection_get_picoquic_connection(connection);

  if (!cnx) {
    log_error("No picoquic connection available for sending");
    ct_message_free(message);
    return -ENOTCONN;
  }

  // Check if connection is ready to send data
  if (picoquic_get_cnx_state(cnx) < picoquic_state_ready) {
    log_warn("ct_connection_t not ready to send data, state: %d", picoquic_get_cnx_state(cnx));
    ct_message_free(message);
    ct_message_context_free(ctx);
    return -EAGAIN;
  }

  if (!ct_connection_stream_is_initialized(connection)) {
    log_trace("First message sent on QUIC stream for connection %s, initializing stream", connection->uuid);
    // Determine stream ID based on connection role (client/server) and stream type (bidirectional/unidirectional)
    ct_connection_assign_next_free_stream(connection, false);
  }

  // Add data to the stream (set_fin=0 since we're not closing the stream)
  uint64_t stream_id = ct_connection_get_stream_id(connection);
  log_debug("Queuing %zu bytes for QUIC, sending on stream %llu, connection: %s", message->length, (unsigned long long)stream_id, connection->uuid);

  int set_fin = 0;
  if (ctx && ct_message_properties_is_final(&ctx->message_properties)) {
    log_debug("Setting FIN on QUIC stream %llu for connection: %s", (unsigned long long)stream_id, connection->uuid);
    set_fin = 1;
  }

  int rc = picoquic_add_to_stream_with_ctx(cnx, stream_id, (const uint8_t*)message->content, message->length, set_fin, connection);

  if (rc != 0) {
    log_error("Error queuing data to QUIC stream: %d", rc);
    if (rc == PICOQUIC_ERROR_INVALID_STREAM_ID) {
      log_error("Invalid stream ID: %llu", (unsigned long long)stream_id);
    }
    ct_message_free(message);
    ct_message_context_free(ctx);
    return -EIO;
  }

  // picoquic_add_to_stream copies the data internally, so we can free the message now
  ct_message_free(message);
  ct_message_context_free(ctx);

  // Reset the timer to ensure data gets processed and sent immediately
  ct_quic_context_t* quic_context = ct_connection_get_quic_context(connection);
  reset_quic_timer(quic_context);

  if (connection->connection_callbacks.sent) {
    connection->connection_callbacks.sent(connection);
  }

  return 0;
}

int quic_listen(ct_socket_manager_t* socket_manager) {
  ct_listener_t* listener = socket_manager->listener;

  // Get certificate from listener's security parameters
  if (!listener->security_parameters) {
    log_error("Security parameters required for QUIC listener");
    return -EINVAL;
  }

  const ct_certificate_bundles_t* cert_bundles =
      listener->security_parameters->security_parameters[SERVER_CERTIFICATE].value.certificate_bundles;

  if (cert_bundles->num_bundles == 0) {
    log_error("No certificate bundle configured for QUIC listener");
    return -EINVAL;
  }

  const char* cert_file = cert_bundles->certificate_bundles[0].certificate_file_name;
  const char* key_file = cert_bundles->certificate_bundles[0].private_key_file_name;

  if (!cert_file || !key_file) {
    log_error("Certificate or key file not configured in listener security parameters");
    return -EINVAL;
  }

  // Create QUIC context for this listener
  ct_quic_context_t* quic_context = ct_create_quic_context(
    cert_file,
    key_file,
    listener,
    NULL,
    listener->security_parameters,
    NULL,
    NULL
  );

  if (!quic_context) {
    log_error("Failed to create QUIC context for listener");
    return -EIO;
  }

  // Set ALPN select callback
  picoquic_set_alpn_select_fn(quic_context->picoquic_ctx, quic_alpn_select_cb);

  ct_quic_group_state_t* listener_group_state = ct_create_quic_group_state();
  if (!listener_group_state) {
    log_error("Failed to allocate QUIC listener group state");
    ct_close_quic_context(quic_context);
    return -ENOMEM;
  }

  listener_group_state->quic_context = quic_context;

  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(listener);

  listener_group_state->udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_quic_buf, on_quic_udp_read);

  if (!listener_group_state->udp_handle) {
    log_error("Failed to create UDP handle for QUIC listener");
    free(listener_group_state);
    ct_close_quic_context(quic_context);
    return -EIO;
  }

  // Store quic_context in udp_handle->data for access in on_quic_udp_read
  listener_group_state->udp_handle->data = quic_context;

  socket_manager->internal_socket_manager_state = listener_group_state;
  socket_manager_increment_ref(socket_manager);

  return 0;
}

int quic_stop_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Stopping QUIC listen");
  ct_quic_group_state_t* group_state = (ct_quic_group_state_t*)socket_manager->internal_socket_manager_state;
  log_trace("Stopping receive on UDP handle: %p", group_state->udp_handle);
  // TODO - write test for receive data on a created ct_connection_t after closing listener
  int rc = uv_udp_recv_stop(group_state->udp_handle);
  if (rc < 0) {
    log_error("Problem with stopping receive: %s\n", uv_strerror(rc));
    return rc;
  }
  uv_close((uv_handle_t*)group_state->udp_handle, quic_closed_udp_handle_cb);

  // Free the QUIC context for this listener
  if (group_state->quic_context) {
    ct_close_quic_context(group_state->quic_context);
    group_state->quic_context = NULL;
  }

  ct_free_quic_group_state(group_state);

  return 0;
}

int quic_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer) {
  (void)peer;
  (void)resolved_peer;
  return -ENOSYS;
}

void quic_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection) {
  // For QUIC, connection_group_state contains the shared UDP handle and picoquic connection
  if (from_connection->connection_group && from_connection->connection_group->connection_group_state) {
    ct_quic_group_state_t* group_state = (ct_quic_group_state_t*)from_connection->connection_group->connection_group_state;

    // Update the UDP handle's data pointer
    if (group_state->udp_handle) {
      group_state->udp_handle->data = to_connection;
    }

    // Update picoquic connection's callback context to point to new connection's group
    if (group_state->picoquic_connection) {
      picoquic_set_callback(group_state->picoquic_connection, picoquic_callback, to_connection->connection_group);
    }

    // Update the connection group's hash table to point to the new connection
    // Remove the old connection pointer and insert the new one
    if (to_connection->connection_group && to_connection->connection_group->connections) {
      log_debug("Updating connection group hash table from %p to %p", (void*)from_connection, (void*)to_connection);
      g_hash_table_remove(to_connection->connection_group->connections, from_connection->uuid);
      g_hash_table_insert(to_connection->connection_group->connections, to_connection->uuid, to_connection);
    }
  }
}
