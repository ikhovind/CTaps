#define _GNU_SOURCE
#include <net/if.h>
#include "quic.h"
#include "connection/connection.h"
#include "connection/connection_group.h"
#include "connection/socket_manager/socket_manager.h"
#include "ctaps.h"
#include "endpoint/local_endpoint.h"
#include "picosocks.h"
#include "protocol/common/socket_utils.h"
#include <glib.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picotls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <uv.h>

#define MICRO_TO_MILLI(us) ((us) / 1000)

// Protocol interface definition (moved from header to access internal struct)
const ct_protocol_impl_t quic_protocol_interface = {
    .name = "QUIC",
    .protocol_enum = CT_PROTOCOL_QUIC,
    .supports_alpn = true,
    .selection_properties = {
      .list = {
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
    .close_listener = quic_close_listener,
    .close_connection = quic_close_connection,
    .close_socket = quic_close_socket,
    .abort = quic_abort,
    .clone_connection = quic_clone_connection,
    .free_socket_state = quic_free_socket_state,
    .close_connection_group = quic_close_connection_group,
    .set_connection_priority = quic_set_connection_priority,
    .free_connection_state = quic_free_state,
    .free_connection_group_state = ct_free_quic_connection_group_state
};

typedef struct ct_quic_send_state_t {
  uv_buf_t* buffer;
  picoquic_cnx_t* cnx;
  struct sockaddr_storage local_address;
  struct sockaddr_storage remote_address;
  ct_connection_group_t* connection_group;
} ct_quic_send_state_t;

ct_quic_send_state_t* ct_quic_send_state_new(uv_buf_t* buffer,
                                             picoquic_cnx_t* cnx,
                                             struct sockaddr_storage local_address,
                                             struct sockaddr_storage remote_address,
                                             ct_connection_group_t* connection_group
                                             ) {
  ct_quic_send_state_t* state = malloc(sizeof(ct_quic_send_state_t));
  if (state) {
    memset(state, 0, sizeof(ct_quic_send_state_t));
    state->cnx = cnx;
    state->buffer = buffer;
    state->local_address = local_address;
    state->remote_address = remote_address;
    state->connection_group = connection_group;
  }
  else {
    log_error("Failed to allocate memory for QUIC send state");
  }
  return state;
}

void ct_quic_send_state_free(ct_quic_send_state_t* state) {
  if (!state) {
    return;
  }
  if (state->buffer) {
    uv_buf_t* buf = state->buffer;
    if (buf->base) {
      free(buf->base);
    }
    free(buf);
  }
  if (state) {
    free(state);
  }
}

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);

bool ct_quic_connection_group_get_close_initiated(const ct_connection_group_t* group) {
  const ct_quic_connection_group_state_t* group_state = group->connection_group_state;
  return group_state ? group_state->close_initiated : false;
}

void ct_quic_connection_group_set_close_initiated(ct_connection_group_t* group, bool val) {
  ct_quic_connection_group_state_t* group_state = group->connection_group_state;
  if (group_state) {
    group_state->close_initiated = val;
  }
}

picoquic_cnx_t* ct_quic_connection_group_get_picoquic_cnx(const ct_connection_group_t* group) {
  const ct_quic_connection_group_state_t* group_state = group->connection_group_state;
  return group_state ? group_state->picoquic_connection : NULL;
}

picoquic_quic_t* ct_connection_get_picoquic_instance(ct_connection_t* connection) {
  if (!connection || !connection->socket_manager || !connection->socket_manager->internal_socket_manager_state) {
    log_error("ct_quic_connection_get_picoquic_instance called with NULL connection or missing socket manager state");
    return NULL;
  }
  ct_quic_socket_state_t* socket_state = ct_connection_get_quic_socket_state(connection);
  return socket_state->picoquic_ctx;
}


ct_quic_connection_group_state_t* ct_create_quic_group_state(void) {
  ct_quic_connection_group_state_t* state = malloc(sizeof(ct_quic_connection_group_state_t));
  if (state) {
    memset(state, 0, sizeof(ct_quic_connection_group_state_t));
  }
  else {
    log_error("Failed to allocate memory for QUIC group state");
  }
  return state;
}

void ct_free_quic_group_state(ct_quic_connection_group_state_t* state) {
  if (state) {
    free(state);
  }
}

void ct_free_quic_stream_state(ct_quic_stream_state_t* state) {
  if (state) {
    free(state);
  }
}

// Forward declarations
void on_socket_timer(uv_timer_t* timer_handle);
void quic_closed_poll_handle_cb(uv_handle_t* handle);

void socket_timer_close_cb(uv_handle_t* handle) {
  ct_quic_socket_state_t* quic_ctx = (ct_quic_socket_state_t*)handle->data;
  if (!quic_ctx) {
    log_warn("QUIC socket timer close callback called with NULL context");
    return;
  }
  if (quic_ctx->ticket_store_path) {
    int rc = picoquic_save_session_tickets(quic_ctx->picoquic_ctx, quic_ctx->ticket_store_path);
    if (rc != 0) {
      log_error("Failed to save QUIC session tickets to store %s: %d", quic_ctx->ticket_store_path, rc);
    } else {
      log_trace("Successfully saved QUIC session tickets to store %s", quic_ctx->ticket_store_path);
    }
  }
  log_debug("Freeing QUIC socket state timer handle");
  if (quic_ctx->poll_handle) {
    log_debug("Stopping and closing socket state poll handle");
    uv_poll_stop(&quic_ctx->poll_handle->poll);
    close(quic_ctx->poll_handle->fd);
    uv_close((uv_handle_t*)&quic_ctx->poll_handle->poll, quic_closed_poll_handle_cb);
  }
  else {
    log_debug("No poll handle to close for QUIC socket state");
    ct_socket_manager_t* socket_manager = quic_ctx->socket_manager;
    socket_manager->callbacks.socket_closed(socket_manager);
  }
}

ct_quic_socket_state_t* ct_quic_socket_state_new(const char* cert_file,
                                          const char* key_file,
                                          ct_socket_manager_t* socket_manager,
                                          const ct_security_parameters_t* security_parameters,
                                          ct_message_t* initial_message // to be freed in case this connection succeeds
                                          ) {
  if (!cert_file || !key_file || !security_parameters) {
    log_error("Certificate, key files and security parameters are required for QUIC context creation");
    return NULL;
  }

  const char* ticket_store_path = ct_security_parameters_get_ticket_store_path(security_parameters);

  ct_quic_socket_state_t* socket_state = malloc(sizeof(ct_quic_socket_state_t));
  if (!socket_state) {
    log_error("Failed to allocate memory for QUIC context");
    return NULL;
  }
  memset(socket_state, 0, sizeof(ct_quic_socket_state_t));

  socket_state->initial_message = initial_message;

  // Store certificate file names (deep copy)
  socket_state->cert_file_name = strdup(cert_file);
  if (!socket_state->cert_file_name) {
    log_error("Failed to duplicate certificate file name");
    free(socket_state);
    return NULL;
  }

  socket_state->key_file_name = strdup(key_file);
  if (!socket_state->key_file_name) {
    log_error("Failed to duplicate key file name");
    free(socket_state->cert_file_name);
    free(socket_state);
    return NULL;
  }

  socket_state->socket_manager = socket_manager;
  socket_manager->internal_socket_manager_state = socket_state;

  // Store ticket store path for 0-RTT session persistence
  if (ticket_store_path) {
    log_trace("Setting ticket store path to %s for QUIC context", ticket_store_path);
    socket_state->ticket_store_path = strdup(ticket_store_path);
    if (!socket_state->ticket_store_path) {
      log_error("Failed to duplicate ticket store path");
      free(socket_state->key_file_name);
      free(socket_state->cert_file_name);
      free(socket_state);
      return NULL;
    }
  } else {
    log_trace("Ticket store path not specified in security parameters for QUIC context");
    socket_state->ticket_store_path = NULL;
  }

  size_t out_num_alpns = 0;

  const char** alpn_strings = ct_security_parameters_get_alpns(security_parameters, &out_num_alpns);
  if (!alpn_strings) {
    log_error("No ALPN strings specified in security parameters for QUIC context");
    free(socket_state->ticket_store_path);
    free(socket_state->key_file_name);
    free(socket_state->cert_file_name);
    return NULL;
  }
  if (out_num_alpns == 0) {
    log_error("ALPN string array is empty in security parameters for QUIC context");
    free(socket_state->ticket_store_path);
    free(socket_state->key_file_name);
    free(socket_state->cert_file_name);
    return NULL;
  }

  const uint8_t* ticket_key = NULL;
  size_t ticket_key_length = 0;

  size_t stek_len = 0;
  const uint8_t* stek = ct_security_parameters_get_session_ticket_encryption_key(security_parameters, &stek_len);
  log_info("Stek address: %p", (void*)stek);
  if (stek) {
    log_trace("Using session ticket encryption key of length %zu from security parameters", ticket_key_length);
    ticket_key = stek;
    ticket_key_length = stek_len;
  }

  // Create picoquic context
  socket_state->picoquic_ctx = picoquic_create(
      MAX_CONCURRENT_QUIC_CONNECTIONS,
      socket_state->cert_file_name,
      socket_state->key_file_name,
      NULL,
      alpn_strings[0],
      picoquic_callback,
      socket_state,
      NULL,
      NULL,
      NULL,
      picoquic_current_time(),
      NULL,
      ticket_store_path,
      ticket_key,
      ticket_key_length
  );
  if (!socket_state->picoquic_ctx) {
    log_error("Failed to create picoquic context");
    free(socket_state->ticket_store_path);
    free(socket_state->key_file_name);
    free(socket_state->cert_file_name);
    free(socket_state);
    return NULL;
  }

  picoquic_set_default_priority(socket_state->picoquic_ctx, CT_CONNECTION_DEFAULT_PRIORITY);
  picoquic_enable_path_callbacks_default(socket_state->picoquic_ctx, 1);


  // Set up timer handle for this context
  socket_state->timer_handle = malloc(sizeof(uv_timer_t));
  if (!socket_state->timer_handle) {
    log_error("Failed to allocate memory for QUIC context timer");
    picoquic_free(socket_state->picoquic_ctx);
    free(socket_state->ticket_store_path);
    free(socket_state->key_file_name);
    free(socket_state->cert_file_name);
    free(socket_state);
    return NULL;
  }

  int rc = uv_timer_init(event_loop, socket_state->timer_handle);
  if (rc < 0) {
    log_error("Error initializing QUIC context timer: %s", uv_strerror(rc));
    free(socket_state->timer_handle);
    picoquic_free(socket_state->picoquic_ctx);
    free(socket_state->ticket_store_path);
    free(socket_state->key_file_name);
    free(socket_state->cert_file_name);
    free(socket_state);
    return NULL;
  }

  socket_state->timer_handle->data = socket_state;

  log_debug("Created QUIC context with cert=%s, key=%s", cert_file, key_file);
  return socket_state;
}

void ct_close_quic_context(ct_quic_socket_state_t* socket_state) {
  if (!socket_state) {
    return;
  }
  log_trace("Closing QUIC context");
  if (socket_state->timer_handle) {
    log_debug("Stopping and closing QUIC context timer");
    // This returns an integer, but for now that is always 0
    int rc = uv_timer_stop(socket_state->timer_handle);
    if (rc < 0) {
      log_error("Error stopping QUIC context timer: %s", uv_strerror(rc));
    }
    uv_close((uv_handle_t*)socket_state->timer_handle, socket_timer_close_cb);
  }
}

// Forward declarations of helper functions
bool ct_connection_stream_is_initialized(ct_connection_t* connection);
void ct_quic_set_connection_stream(ct_connection_t* connection, uint64_t stream_id);
void ct_connection_assign_next_free_stream(ct_connection_t* connection, bool is_unidirectional);
uint64_t ct_connection_get_stream_id(const ct_connection_t* connection);
picoquic_cnx_t* ct_connection_get_picoquic_connection(const ct_connection_t* connection);

bool ct_connection_stream_is_initialized(ct_connection_t* connection) {
  if (!connection) {
    log_error("Cannot check if connection stream is initialized, connection is NULL");
    return false;
  }
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    log_error("Cannot check if connection stream is initialized, stream state is NULL");
    return false;
  }
  return stream_state->stream_initialized;
}

void ct_quic_set_connection_stream(ct_connection_t* connection, uint64_t stream_id) {
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    log_error("Cannot set conncetion stream for connection %s, stream state is NULL", connection->uuid);
    return;
  }
  
  log_debug("Setting QUIC stream ID %llu for connection %s", (unsigned long long)stream_id, connection->uuid);
  stream_state->stream_id = stream_id;
  stream_state->stream_initialized = true;
}

void ct_connection_assign_next_free_stream(ct_connection_t* connection, bool is_unidirectional) {
  ct_quic_connection_group_state_t* group_state = connection->connection_group->connection_group_state;
  picoquic_cnx_t* cnx = group_state->picoquic_connection;

  uint64_t next_stream_id = picoquic_get_next_local_stream_id(cnx, is_unidirectional);
  log_debug("Assigned QUIC stream ID: %llu (unidirectional: %d)", (unsigned long long)next_stream_id, is_unidirectional);

  ct_quic_set_connection_stream(connection, next_stream_id);
  int rc = picoquic_set_app_stream_ctx(cnx, next_stream_id, connection);
  if (rc < 0) {
    log_error("Failed to set stream context for first connection: %d", rc);
    return;
  }
}

uint64_t ct_connection_get_stream_id(const ct_connection_t* connection) {
  if (!connection) {
    log_error("Cannot get stream ID, connection is NULL");
    return 0;
  }
  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
  if (!stream_state) {
    log_error("Cannot get stream ID for connection %s, stream state is NULL", connection->uuid);
    return 0;
  }
  return stream_state->stream_id;
}

ct_quic_connection_group_state_t* ct_connection_get_quic_group_state(const ct_connection_t* connection) {
  if (!connection || !connection->connection_group || !connection->connection_group->connection_group_state) {
    log_error("Cannot get QUIC group state, connection or group state is NULL");
    log_debug("conn=%p, group=%p, group_state=%p", 
              (void*)connection,
              (void*)(connection ? connection->connection_group : NULL), 
              (void*)(connection && connection->connection_group ? connection->connection_group->connection_group_state : NULL));
    return NULL;
  }
  return (ct_quic_connection_group_state_t*)connection->connection_group->connection_group_state;
}

ct_quic_stream_state_t* ct_connection_get_stream_state(const ct_connection_t* connection) {
  if (!connection || !connection->internal_connection_state) {
    log_error("Cannot get stream state, connection or internal state is NULL");
    log_debug("conn=%p, internal_state=%p", 
              (void*)connection,
              (void*)(connection ? connection->internal_connection_state : NULL));
    return NULL;
  }
  return (ct_quic_stream_state_t*)connection->internal_connection_state;
}

picoquic_cnx_t* ct_connection_get_picoquic_connection(const ct_connection_t* connection) {
  ct_quic_connection_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  assert(group_state);
  log_debug("Retrieved picoquic connection %p for connection", (void*)group_state->picoquic_connection);
  return group_state->picoquic_connection;
}

ct_quic_socket_state_t* ct_connection_get_quic_socket_state(const ct_connection_t* connection) {
  if (!connection || !connection->socket_manager) {
    log_error("Cannot get QUIC socket state, connection or socket manager is NULL");
    log_debug("Connection: %p, connection->socket_manager: %p", connection, connection ? connection->socket_manager : NULL);
    return NULL;
  }
  return (ct_quic_socket_state_t*)connection->socket_manager->internal_socket_manager_state;
}


size_t quic_alpn_select_cb(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count) {
  log_trace("QUIC server alpn select cb");

  (void)count;

  ct_quic_socket_state_t* quic_context = picoquic_get_default_callback_context(quic);
  if (!quic_context || !quic_context->socket_manager->listener) {
    log_error("ALPN select callback: no listener associated with QUIC context");
    return count;  // Return count to indicate no match
  }

  ct_listener_t* listener = quic_context->socket_manager->listener;

  size_t num_alpns = 0;
  const char** listener_alpns = ct_security_parameters_get_alpns(listener->security_parameters, &num_alpns);
  if (num_alpns == 0) {
    log_warn("Listener has no ALPNs configured for selection");
    return count;
  }

  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < num_alpns; j++) {
      if (strcmp((const char*)list[i].base, listener_alpns[j]) == 0) {
        log_trace("Selected ALPN: %.*s", (int)list[i].len, list[i].base);
        return i;
      }
    }
  }
  log_warn("No compatible ALPN found for attempted connection to listener");
  return count;
}

void reset_quic_timer(ct_quic_socket_state_t* quic_context) {
  if (!quic_context || !quic_context->picoquic_ctx || !quic_context->timer_handle) {
    log_error("Cannot reset QUIC timer: invalid context");
    log_debug("ctx=%p, ctx->quic_ctx=%p, ctx->timer_handle=%p", (void*)quic_context, (void*)(quic_context ? quic_context->picoquic_ctx : NULL), (void*)(quic_context ? quic_context->timer_handle : NULL));
    return;
  }
  uint64_t next_wake_delay = picoquic_get_next_wake_delay(quic_context->picoquic_ctx, picoquic_get_quic_time(quic_context->picoquic_ctx), INT64_MAX - 1);
  log_trace("Resetting QUIC timer to fire in %llu ms", (unsigned long long)MICRO_TO_MILLI(next_wake_delay));
  uv_timer_start(quic_context->timer_handle, on_socket_timer, MICRO_TO_MILLI(next_wake_delay), 0);
}

void quic_closed_poll_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle for QUIC connection");
  ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)handle->data;
  socket_manager->callbacks.socket_closed(socket_manager);
}

int handle_closed_picoquic_connection(ct_connection_group_t* connection_group) {
  log_debug("Handling closed picoquic connection for connection group %s", connection_group->connection_group_id);

  ct_socket_manager_t* socket_manager = NULL;

  GPtrArray* connections_to_notify = g_ptr_array_new();
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, connection_group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* connection = (ct_connection_t*)value;
    socket_manager = connection->socket_manager;
    if (!ct_connection_is_closed(connection)) {
      g_ptr_array_add(connections_to_notify, connection);
    }
  }

  for (guint i = 0; i < connections_to_notify->len; i++) {
      ct_connection_t* conn = g_ptr_array_index(connections_to_notify, i);
      socket_manager->callbacks.closed_connection(conn);
  }
  g_ptr_array_free(connections_to_notify, true);
  return 0;
}

int handle_aborted_picoquic_connection_group(ct_connection_group_t* connection_group) {
  log_debug("Handling closed picoquic connection for connection group %s", connection_group->connection_group_id);

  ct_socket_manager_t* socket_manager = NULL;

  GPtrArray* connections_to_notify = g_ptr_array_new();
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, connection_group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* connection = (ct_connection_t*)value;
    socket_manager = connection->socket_manager;
    if (!ct_connection_is_closed(connection)) {
      g_ptr_array_add(connections_to_notify, connection);
    }
  }

  for (guint i = 0; i < connections_to_notify->len; i++) {
      ct_connection_t* conn = g_ptr_array_index(connections_to_notify, i);
      if(socket_manager && socket_manager->callbacks.aborted_connection) {
        socket_manager->callbacks.aborted_connection(conn);
      }
      else {
        log_debug("No connection closed callback set for connection: %s", conn->uuid);
      }
  }
  g_ptr_array_free(connections_to_notify, true);
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

  // Check if connection can still receive data
  if (!ct_connection_can_receive(connection)) {
    log_error("Received data on stream after FIN was already received for connection %s", connection->uuid);
    return -EPIPE;
  }

  log_trace("Connection %s received %zu bytes of data", connection->uuid, length);

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

  log_debug("Handling FIN for connection %s", connection->uuid);

  // RFC 9622: Set canReceive to false when Final message received
  ct_connection_set_can_receive(connection, false);

  // Check if both send and receive directions are closed
  bool can_send = ct_connection_can_send(connection);

  size_t num_active = 0;
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, connection->connection_group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* conn = (ct_connection_t*)value;
    if (ct_connection_can_send(conn) || ct_connection_can_receive(conn)) {
      log_debug("Connection %s is still active (can_send=%d, can_receive=%d)", conn->uuid, ct_connection_can_send(conn), ct_connection_can_receive(conn));
      num_active++;
    }
  }

  if (num_active == 0) {
      log_debug("No more active connections in group after receiving FIN, closing entire QUIC connection");
      ct_quic_connection_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
      picoquic_close(group_state->picoquic_connection, 0);
  }
  else if (!can_send) {
    // Both directions closed, but some streams still active, just notify socket manager
    log_info("Both send and receive sides closed for connection %s, closing connection", connection->uuid);
    ct_socket_manager_t* socket_manager = connection->socket_manager;
    if (socket_manager && socket_manager->callbacks.closed_connection) {
      socket_manager->callbacks.closed_connection(connection);
    }
  }
}
static int resolve_or_create_stream_connection(
    picoquic_cnx_t* cnx,
    ct_connection_group_t* connection_group,
    void* v_stream_ctx,
    uint64_t stream_id,
    ct_connection_t** out_connection,
    bool* out_is_new_connection)
{
    *out_is_new_connection = false;
    ct_connection_t* connection = (ct_connection_t*)v_stream_ctx;

    if (!connection) {
        log_debug("Received data on new stream %llu from remote", (unsigned long long)stream_id);

        connection = ct_connection_group_get_first(connection_group);
        assert(connection);

        if (ct_connection_stream_is_initialized(connection)) {
            log_debug("First connection already has stream %llu, creating new for stream %llu",
                      (unsigned long long)ct_connection_get_stream_id(connection),
                      (unsigned long long)stream_id);
            *out_is_new_connection = true;
            connection = ct_connection_create_clone(
                connection, connection->socket_manager, connection->framer_impl, ct_quic_stream_state_new());
            if (!connection) {
                log_error("Failed to create cloned connection for new stream");
                return -ENOMEM;
            }
        }

        bool server_initiated = (stream_id & 1U) != 0;
        bool unidirectional = (stream_id & 2U) != 0;
        bool can_send = false;
        bool can_receive = false;
        if (picoquic_is_client(cnx)) {
          log_debug("New stream %llu is for client connection, determining send/receive capabilities based on stream ID", (unsigned long long)stream_id);
          can_receive = server_initiated || !unidirectional;
          can_send = !server_initiated || !unidirectional;
        }
        else {
          log_debug("New stream %llu is for server connection, determining send/receive capabilities based on stream ID", (unsigned long long)stream_id);
          can_receive = !server_initiated || !unidirectional;
          can_send = server_initiated || !unidirectional;
        }
        log_debug("Stream %llu capabilities - can_send: %d, can_receive: %d", (unsigned long long)stream_id, can_send, can_receive);
        ct_connection_set_can_send(connection, can_send);
        ct_connection_set_can_receive(connection, can_receive);
        ct_quic_set_connection_stream(connection, stream_id);
        int rc = picoquic_set_app_stream_ctx(cnx, stream_id, connection);
        if (rc < 0) {
            log_error("Failed to set stream context: %d", rc);
            return rc;
        }
    }

    *out_connection = connection;
    return 0;
}

int probe_all_paths(const ct_connection_group_t* group) {
    log_debug("Probing all paths for connection group %s", group->connection_group_id);
    ct_connection_t* connection = ct_connection_group_get_first(group);
    picoquic_cnx_t* cnx = ct_quic_connection_group_get_picoquic_cnx(group);
    int at_least_one_success = 0;

    log_debug("Connection has %zu remote endpoints and %zu local endpoints configured",
              ct_connection_get_num_remote_endpoints(connection),
              ct_connection_get_num_local_endpoints(connection));
    for (size_t i = 0; i < ct_connection_get_num_remote_endpoints(connection); i++) {
        const ct_remote_endpoint_t* remote_endpoint =
            &ct_connection_get_remote_endpoints_list(connection)[i];

      for (size_t j = 0; j < ct_connection_get_num_local_endpoints(connection); j++) {
        if (j == connection->active_local_endpoint && i == connection->active_remote_endpoint) {
          continue;
        }
        const ct_local_endpoint_t* local_endpoint = &ct_connection_get_local_endpoints_list(connection)[j];

        if (local_endpoint->data.resolved_address.ss_family != remote_endpoint->data.resolved_address.ss_family) {
          log_debug("Skipping probing local endpoint %zu to remote endpoint %zu due to address family mismatch",
                    j, i);
          continue;
        }

        char from_ip[INET6_ADDRSTRLEN];
        char to_ip[INET6_ADDRSTRLEN];
        uint16_t from_port = 0;
        uint16_t dest_port = 0;

        const struct sockaddr_storage* local_ss = &local_endpoint->data.resolved_address;
        const struct sockaddr_storage* remote_ss = &remote_endpoint->data.resolved_address;

        ct_get_addr_string(local_ss, from_ip, sizeof(from_ip), &from_port);
        ct_get_addr_string(remote_ss, to_ip, sizeof(to_ip), &dest_port);

        log_debug("Probing picoquic path: %s:%d -> %s:%d",
                 from_ip, from_port, to_ip, dest_port);

        int rc = picoquic_probe_new_path(
            cnx,
            (const struct sockaddr*)&remote_endpoint->data.resolved_address,
            (const struct sockaddr*)&local_endpoint->data.resolved_address,  /* let picoquic pick the local address */
            picoquic_get_quic_time(ct_connection_get_picoquic_instance(connection))
        );
        if (rc < 0) {
            log_warn("Failed to probe path to remote endpoint %zu with error code: %d", i, rc);
        } else {
            log_debug("Probing remote endpoint %zu", i);
            at_least_one_success = 1;
        }
      }
    }

    if (!at_least_one_success) {
        log_error("Failed to probe any paths for connection group %s",
                  group->connection_group_id);
        return -EIO;
    }
    return at_least_one_success;
}

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
  int rc = 0;
  ct_connection_group_t* connection_group = (ct_connection_group_t*)callback_ctx;
  ct_connection_t* connection = NULL;
  log_trace("Received picoquic callback event: %d", fin_or_event);
  assert(connection_group);

  switch (fin_or_event) {
    case picoquic_callback_ready:
      log_debug("QUIC connection is ready, invoking CTaps callback");
      // check if is server or client connection and print
      if (picoquic_is_client(cnx)) {
        log_debug("Picoquic callback for client connection");
      }
      else {
        log_debug("Picoquic callback for server connection");
      }
      probe_all_paths(connection_group);

      // the picoquic_callback_ready event is per-cnx.
      // This means that this callback only happens once per connection group.
      // We therefore know that the connection group only has one connection at this point.
      // We build this connection group in the on_quic_udp_read, when initially receiving
      // QUIC data over our UDP socket, so we know it exists
      connection = ct_connection_group_get_first(connection_group);
      assert(connection);

      ct_socket_manager_t* socket_manager = connection->socket_manager;
      ct_quic_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
      if (socket_state->initial_message) {
        ct_message_free(socket_state->initial_message);
        socket_state->initial_message = NULL;
      }
      if (socket_state->initial_message_context) {
        ct_message_context_free(socket_state->initial_message_context);
        socket_state->initial_message_context = NULL;
      }

      if (ct_connection_is_server(connection)) {
        log_debug("Server connection ready, notifying listener");
        int rc = resolve_local_endpoint_from_poll(socket_state->poll_handle, connection);
        if (rc < 0) {
          log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
        }
        log_debug("Number of connections in socket manager: %zu", g_slist_length(socket_manager->all_connections));
        socket_manager->callbacks.connection_received(socket_manager->listener, connection);
      }
      else if (ct_connection_is_client(connection)) {
        if (picoquic_tls_is_psk_handshake(cnx)) {
          log_trace("Client connection was established with 0-RTT");
          ct_quic_connection_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
          if (group_state->attempted_early_data) {
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
        socket_manager->callbacks.connection_ready(connection);
      }
      else {
        log_error("Unknown connection role in picoquic ready callback");
      }
      break;
    case picoquic_callback_stream_data: {
      log_debug("Received %zu bytes on stream %llu", length, (unsigned long long)stream_id);
      bool is_new_connection = false;
      int rc = resolve_or_create_stream_connection(
          cnx, connection_group, v_stream_ctx, stream_id, &connection, &is_new_connection);
      if (rc < 0) {
        return rc;
      }
      rc = handle_stream_data(connection, bytes, length);
      if (rc < 0) {
        log_error("Error handling stream data: %d", rc);
        return rc;
      }
      if (is_new_connection) {
        connection->socket_manager->callbacks.connection_ready(connection);
      }
      break;
    }
    case picoquic_callback_stream_fin: {
      log_debug("Received QUIC FIN on stream %llu, data length: %zu",
                (unsigned long long)stream_id, length);
      bool is_new_connection = false;
      int rc = resolve_or_create_stream_connection(
          cnx, connection_group, v_stream_ctx, stream_id, &connection, &is_new_connection);
      if (rc < 0) {
        return rc;
      }

      if (length > 0) {
        rc = handle_stream_data(connection, bytes, length);
        if (rc != 0) {
          log_error("Error handling data received with FIN: %d", rc);
          return rc;
        }
      }
      handle_stream_fin(connection);
      if (is_new_connection) {
        connection->socket_manager->callbacks.connection_ready(connection);
      }
      break;
    }
    case picoquic_callback_stream_reset:
      log_info("Received RESET on stream %llu", (unsigned long long)stream_id);
      if (v_stream_ctx) {
        connection = (ct_connection_t*)v_stream_ctx;
        log_info("Peer reset stream for connection %p", (void*)connection);
        connection->socket_manager->callbacks.aborted_connection(connection);
      } else {
        log_warn("Received RESET on stream %llu but no stream context available", (unsigned long long)stream_id);
      }
      break;
    case picoquic_callback_stop_sending:
      log_info("Received STOP_SENDING on stream %llu", (unsigned long long)stream_id);
      if (v_stream_ctx) {
        connection = (ct_connection_t*)v_stream_ctx;
        log_info("Peer sent STOP_SENDING for connection %p", (void*)connection);
        ct_connection_set_can_send(connection, false);
      } else {
        log_warn("Received STOP_SENDING on stream %llu but no stream context available", (unsigned long long)stream_id);
      }
      break;
    case picoquic_callback_stateless_reset:
      // Reset the active connection counter since entire QUIC connection is closed
      log_debug("Picoquic stateless reset callback received, treating as aborted connection for entire connection group");
      ct_quic_connection_group_set_close_initiated(connection_group, true);
      rc = handle_aborted_picoquic_connection_group(connection_group);
      if (rc != 0) {
        log_error("Error handling stateless reset for connection group: %d", rc);
        return rc;
      }
      break;
    case picoquic_callback_close:
      log_debug("Picoquic connection closed callback received");

      uint64_t error = picoquic_get_remote_error(cnx);
      ct_quic_connection_group_set_close_initiated(connection_group, true);
      if (error != 0) {
        log_info("QUIC connection closed by peer with error code: %llu", (unsigned long long)error);
        log_debug("Closing connection group %s due to peer close with error", connection_group->connection_group_id);
        rc = handle_aborted_picoquic_connection_group(connection_group);
      } else {
        log_debug("QUIC connection %s closed by peer without error", connection_group->connection_group_id);
        rc = handle_closed_picoquic_connection(connection_group);
      }

      if (rc != 0) {
        log_error("Error handling closed picoquic connection: %d", rc);
        return rc;
      }
      break;
    case picoquic_callback_application_close:
      {
        log_info("Received picoquic_callback_application_close event, waiting for close callback");
      }
      break;
    case picoquic_callback_request_alpn_list:
      log_warn("ALPN list requested in callback, should never happen");
      return -EINVAL;
      break;
    case picoquic_callback_path_available: {
      log_debug("Picoquic path available callback received");

      struct sockaddr_storage to_address = {0};
      int rc = picoquic_get_path_addr(cnx, stream_id, 2, &to_address);
      if (rc < 0) {
        log_error("Failed to get path address after path available callback: %d", rc);
        return rc;
      }

      char to_ip[INET6_ADDRSTRLEN];
      uint16_t to_port = 0;
      ct_get_addr_string(&to_address, to_ip, sizeof(to_ip), &to_port);

      struct sockaddr_storage from_address = {0};
      rc = picoquic_get_path_addr(cnx, stream_id, 1, &from_address);
      if (rc < 0) {
        log_error("Failed to get path address after path available callback: %d", rc);
        return rc;
      }

      char from_ip[INET6_ADDRSTRLEN];
      uint16_t from_port = 0;
      ct_get_addr_string(&from_address, from_ip, sizeof(from_ip), &from_port);

      log_debug("New path available for QUIC: from %s:%d to %s:%d", from_ip, from_port, to_ip, to_port);


      ct_remote_endpoint_t remote_endpoint = {0};
      ct_remote_endpoint_from_sockaddr(&remote_endpoint, &to_address);
      rc = ct_connection_group_set_active_remote_endpoint(connection_group, &remote_endpoint);
      if (rc != 0) {
        log_error("Failed to set active remote for connection group, error code: %d", rc);
        return rc;
      }

      ct_local_endpoint_t local_endpoint = {0};
      ct_local_endpoint_from_sockaddr(&local_endpoint, &from_address);
      rc = ct_connection_group_set_active_local_endpoint(connection_group, &local_endpoint);
      if (rc != 0) {
        log_error("Failed to set active local for connection group, error code: %d", rc);
        return rc;
      }

      break;
    }
    case picoquic_callback_path_suspended: { /* An available path is suspended */
      log_debug("Picoquic path suspended callback received");
      break;
    }
    case picoquic_callback_path_deleted: { /* An existing path has been deleted */
      log_debug("Picoquic path deleted callback received");
      break;
    }
    default:
      log_debug("Unhandled callback event: %d", fin_or_event);
      break;
  }
  return 0;
}

void on_quic_udp_send(uv_udp_send_t* req, int status) {
  log_trace("Sent QUIC packet over UDP");

  ct_quic_send_state_t* send_state = req->data;
  
  if (status) {
    log_warn("Send error: %s\n", uv_strerror(status));
    ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)req->handle->data;
    ct_quic_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
    picoquic_notify_destination_unreachable(
        send_state->cnx,
        picoquic_get_quic_time(socket_state->picoquic_ctx),
        (struct sockaddr*)&send_state->local_address,
        (struct sockaddr*)&send_state->remote_address,
        0, status);
  }
  ct_quic_send_state_free(send_state);
  free(req);
}

static int ct_quic_send_packet_with_pktinfo(
  ct_udp_poll_handle_t* poll_handle,
  unsigned char* send_buffer_base,
  size_t send_length,
  struct sockaddr_storage* from_address,
  struct sockaddr_storage* to_address,
  int if_index
)
{
  char from_ip[INET6_ADDRSTRLEN];
  uint16_t from_port;

  char to_ip[INET6_ADDRSTRLEN];
  uint16_t to_port;

  ct_get_addr_string(from_address, from_ip, sizeof(from_ip), &from_port);
  ct_get_addr_string(to_address, to_ip, sizeof(to_ip), &to_port);

  log_debug("Sending QUIC packet from %s:%d to %s:%d with pktinfo on interface index %d",
            from_ip, from_port, to_ip, to_port, if_index);

  struct iovec iov = {
    .iov_base = send_buffer_base,
    .iov_len  = send_length,
  };

  // Sized to fit whichever pktinfo variant is larger
  char cmsg_buf[CMSG_SPACE(sizeof(struct in6_pktinfo))] = {0};

  struct msghdr msg = {
    .msg_name       = (struct sockaddr*)to_address,
    .msg_namelen    = (to_address->ss_family == AF_INET6)
                        ? sizeof(struct sockaddr_in6)
                        : sizeof(struct sockaddr_in),
    .msg_iov        = &iov,
    .msg_iovlen     = 1,
    .msg_control    = cmsg_buf,
    .msg_controllen = sizeof(cmsg_buf),
  };

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);

  if (to_address->ss_family == AF_INET6) {
    struct sockaddr_in6* src = (struct sockaddr_in6*)from_address;

    cmsg->cmsg_level = IPPROTO_IPV6;
    cmsg->cmsg_type  = IPV6_PKTINFO;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(struct in6_pktinfo));

    struct in6_pktinfo* pktinfo = (struct in6_pktinfo*)CMSG_DATA(cmsg);
    pktinfo->ipi6_addr    = src->sin6_addr;
    pktinfo->ipi6_ifindex = if_index;

    msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
  } else {
    struct sockaddr_in* src = (struct sockaddr_in*)from_address;

    cmsg->cmsg_level = IPPROTO_IP;
    cmsg->cmsg_type  = IP_PKTINFO;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(struct in_pktinfo));

    struct in_pktinfo* pktinfo = (struct in_pktinfo*)CMSG_DATA(cmsg);
    pktinfo->ipi_spec_dst = src->sin_addr;
    pktinfo->ipi_ifindex  = if_index;

    msg.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
  }

  
  ssize_t sent = sendmsg(poll_handle->fd, &msg, 0);
  if (sent < 0) {
    log_error("sendmsg with pktinfo failed: %s", strerror(errno));
    return -errno;
  }

  log_trace("Sent QUIC packet of length %zu via sendmsg/pktinfo", send_length);
  return 0;
}

void on_quic_poll_read(ct_socket_manager_t* socket_manager,
                       const uint8_t* buf, ssize_t nread,
                       const struct sockaddr* addr_from,
                       const struct sockaddr* addr_to)
{
    if (nread < 0) {
        log_error("recvmsg error in poll read");
        return;
    }
    if (nread == 0) return;

    ct_quic_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
    if (!socket_state || !socket_state->picoquic_ctx) {
        log_error("No QUIC context associated with poll handle");
        return;
    }

    picoquic_quic_t* picoquic_ctx = socket_state->picoquic_ctx;
    picoquic_cnx_t* cnx = NULL;

    int rc = picoquic_incoming_packet_ex(
        picoquic_ctx,
        (uint8_t*)buf,
        nread,
        (struct sockaddr*)addr_from,
        (struct sockaddr*)addr_to,
        0,
        0,
        &cnx,
        picoquic_get_quic_time(picoquic_ctx)
    );
    if (rc != 0) {
        log_error("Error processing incoming QUIC packet: %d", rc);
    }

    // Same new-connection handling as when first reading
    if (cnx && picoquic_get_callback_context(cnx) == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
      if (!socket_manager->listener || ct_listener_is_closed(socket_manager->listener)) {
        log_debug("Received packet for QUIC cnx but no listener available to accept it, ignoring");
        return;
      }
      log_info("Received packet for new QUIC cnx for listener");
      ct_listener_t* listener = socket_state->socket_manager->listener;
      if (!listener || ct_listener_is_closed(listener)) {
          log_error("No listener associated with QUIC context for incoming connection");
          return;
      }

      ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
      if (!remote_endpoint) {
        log_error("Failed to allocate memory for remote endpoint");
        return;
      }
      rc = ct_remote_endpoint_from_sockaddr(remote_endpoint, (struct sockaddr_storage*)addr_from);
      if (rc < 0) {
        log_error("Failed to create remote endpoint from sockaddr: %s", uv_strerror(rc));
        free(remote_endpoint);
        return;
      }
      ct_connection_t* connection = ct_connection_create_server_connection(listener->socket_manager, remote_endpoint, listener->local_endpoint, listener->security_parameters, &listener->connection_callbacks, NULL);
      if (!connection) {
        log_error("Failed to create new ct_connection_t for incoming QUIC connection");
        return;
      }

      ct_remote_endpoint_free(remote_endpoint);

      log_trace("Created new ct_connection_t object for received QUIC cnx: %s", connection->uuid);

      // Set picoquic callback to connection group (not individual connection)
      picoquic_set_callback(cnx, picoquic_callback, connection->connection_group);

      // Allocate shared group state for this quic group
      ct_quic_connection_group_state_t* group_state = ct_create_quic_group_state();
      if (!group_state) {
        log_error("Failed to allocate memory for QUIC group state");
        ct_connection_free(connection);
        return;
      }
      group_state->picoquic_connection = cnx;

      log_trace("Setting up received ct_connection_t state for new ct_connection_t");
      ct_quic_socket_state_t* socket_state = (ct_quic_socket_state_t*)listener->socket_manager->internal_socket_manager_state;
      rc = resolve_local_endpoint_from_poll(socket_state->poll_handle, connection);
      if (rc < 0) {
        log_error("Could not get UDP socket name for QUIC connection: %s", uv_strerror(rc));
        free(group_state);
        ct_connection_free(connection);
        return;
      }

      connection->connection_group->connection_group_state = group_state;

      // Allocate per-stream state (stream_id will be set when stream is created)
      ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
      if (!stream_state) {
        log_error("Failed to allocate memory for QUIC stream state");
        free(group_state);
        ct_connection_free(connection);
        return;
      }
      stream_state->stream_id = 0;
      stream_state->stream_initialized = false;
      connection->internal_connection_state = stream_state;
      log_trace("Done setting up received QUIC connection state");
    }

    reset_quic_timer(socket_state);
}

void on_socket_timer(uv_timer_t* timer_handle) {
  ct_quic_socket_state_t* socket_state = (ct_quic_socket_state_t*)timer_handle->data;
  if (!socket_state || !socket_state->picoquic_ctx) {
    log_error("QUIC context timer triggered but context is invalid");
    return;
  }

  log_trace("QUIC context timer triggered, checking for new QUIC packets to send");

  picoquic_quic_t* picoquic_ctx = socket_state->picoquic_ctx;
  size_t send_length = 0;

  struct sockaddr_storage from_address = {0};
  struct sockaddr_storage to_address = {0};
  int if_index = 0;
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

    log_trace("Prepared QUIC packet of length %zu", send_length);
    if (send_length > 0) {
      int rc = ct_quic_send_packet_with_pktinfo(
        socket_state->poll_handle,
        send_buffer_base,
        send_length,
        &from_address,
        &to_address,
        if_index
      );
        free(send_buffer_base);  // synchronous, no async callback to free it
      if (rc < 0) {
        log_warn("Failed to send QUIC packet: %d", rc);

        picoquic_notify_destination_unreachable(
            last_cnx,
            picoquic_get_quic_time(socket_state->picoquic_ctx),
            (struct sockaddr*)&from_address,
            (struct sockaddr*)&to_address,
            if_index, rc);
          break;
        }
      log_trace("Sent QUIC packet of length %zu", send_length);
    } else {
      log_trace("No QUIC data to send at this time");
      // No data to send, free the buffer
      free(send_buffer_base);
    }
  } while (send_length > 0);
  log_trace("Finished sending QUIC packets");

  reset_quic_timer(socket_state);
}

// TODO - this and quic_init shares a lot of code, should refactor to common function
int quic_init_with_send(ct_connection_t* connection, ct_message_t* initial_message, ct_message_context_t* initial_message_context) {
  log_info("Initializing standalone QUIC connection and attempting early data");

  // Get certificate from security parameters
  if (!connection->security_parameters) {
    log_error("Security parameters required for QUIC connection");
    return -EINVAL;
  }

  size_t bundle_count = ct_security_parameters_get_client_certificate_count(connection->security_parameters);

  if (bundle_count == 0) {
    log_error("No client certificate configured for QUIC init");
    return -EINVAL;
  }

  const char* cert_file = ct_security_parameters_get_client_certificate_file(connection->security_parameters, 0);
  const char* key_file = ct_security_parameters_get_client_certificate_key_file(connection->security_parameters, 0);

  if (!cert_file || !key_file) {
    log_error("Certificate or key file not configured in security parameters");
    log_debug("cert_file=%p, key_file=%p", (void*)cert_file, (void*)key_file);
    return -EINVAL;
  }

  ct_quic_socket_state_t* quic_context = ct_quic_socket_state_new(
    cert_file,
    key_file,
    connection->socket_manager,
    connection->security_parameters,
    initial_message
  );

  if (!quic_context) {
    log_error("Failed to create QUIC context for client connection");
    return -EIO;
  }

  uint64_t current_time = picoquic_get_quic_time(quic_context->picoquic_ctx);

  ct_udp_poll_handle_t* poll_handle = create_udp_poll_on_local(ct_connection_get_active_local_endpoint(connection));
  if (!poll_handle) {
    log_error("Failed to create UDP handle for QUIC connection");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  poll_handle->poll.data = connection->socket_manager;

  // Store quic_context in udp_handle->data for access in on_quic_udp_read
  quic_context->poll_handle = poll_handle;


  connection->connection_group->connection_group_state = ct_create_quic_group_state();
  if (!connection->connection_group->connection_group_state) {
    log_error("Failed to allocate QUIC group state");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
  if (!stream_state) {
    log_error("Failed to allocate QUIC stream state");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return -ENOMEM;
  }
  stream_state->stream_id = 0;
  stream_state->stream_initialized = false;
  connection->internal_connection_state = stream_state;

  int rc = resolve_local_endpoint_from_poll(poll_handle, connection);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  size_t alpn_count = 0;
  const char** alpn_strings = ct_security_parameters_get_alpns(connection->security_parameters, &alpn_count);
  if (alpn_count == 0) {
    log_error("No ALPN strings configured for QUIC connection");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  ct_quic_connection_group_state_t* group_state = (ct_quic_connection_group_state_t*)connection->connection_group->connection_group_state;

  const ct_remote_endpoint_t* remote_endpoint = ct_connection_get_active_remote_endpoint(connection);

  group_state->picoquic_connection = picoquic_create_cnx(
    quic_context->picoquic_ctx,
    picoquic_null_connection_id,
    picoquic_null_connection_id,
    (struct sockaddr*) &remote_endpoint->data.resolved_address,
    current_time,
    1,
    ct_security_parameters_get_server_name_identification(connection->security_parameters),
    alpn_strings[0], // We create separate candidates for each ALPN to support 0-rtt (see candidate gathering code)
    1
  );

  log_trace("Setting callback context to connection group: %p", (void*)connection->connection_group);

  // Set picoquic callback to connection group
  picoquic_set_callback(group_state->picoquic_connection, picoquic_callback, connection->connection_group);

  bool is_final = initial_message_context && ct_message_properties_get_final(ct_message_context_get_message_properties(initial_message_context));

  ct_connection_assign_next_free_stream(connection, false);
  rc = picoquic_add_to_stream_with_ctx(
      group_state->picoquic_connection,
      ct_connection_get_stream_id(connection),
      (const uint8_t*)initial_message->content,
      initial_message->length,
      is_final,
      connection
  );

  if (rc < 0) {
    log_error("Failed to add initial message to QUIC stream: %d", rc);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }
  rc = picoquic_set_app_stream_ctx(group_state->picoquic_connection, ct_connection_get_stream_id(connection), connection);

  if (rc < 0) {
    log_error("Failed to set stream context for first connection: %d", rc);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  group_state->attempted_early_data = true;

  rc = picoquic_start_client_cnx(group_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  reset_quic_timer(quic_context);
  log_trace("Successfully initiated standalone QUIC connection %p", (void*)connection);
  return 0;
}

int quic_init(ct_connection_t* connection) {
  log_info("Initializing standalone QUIC connection");

  // Get certificate from security parameters
  if (!connection->security_parameters) {
    log_error("Security parameters required for QUIC connection");
    return -EINVAL;
  }

  size_t bundle_count = ct_security_parameters_get_client_certificate_count(connection->security_parameters);

  if (bundle_count == 0) {
    log_error("No client certificate configured for QUIC client");
    return -EINVAL;
  }

  const char* cert_file = ct_security_parameters_get_client_certificate_file(connection->security_parameters, 0);
  const char* key_file = ct_security_parameters_get_client_certificate_key_file(connection->security_parameters, 0);

  if (!cert_file || !key_file) {
    log_error("Certificate or key file not configured in security parameters");
    return -EINVAL;
  }

  ct_quic_socket_state_t* quic_context = ct_quic_socket_state_new(
    cert_file,
    key_file,
    connection->socket_manager,
    connection->security_parameters,
    NULL
  );

  if (!quic_context) {
    log_error("Failed to create QUIC context for client connection");
    return -EIO;
  }

  uint64_t current_time = picoquic_get_quic_time(quic_context->picoquic_ctx);

  ct_udp_poll_handle_t* poll_handle = create_udp_poll_on_local(ct_connection_get_active_local_endpoint(connection));
  if (!poll_handle) {
    log_error("Failed to create UDP handle for QUIC connection");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  poll_handle->poll.data = connection->socket_manager;
  quic_context->poll_handle = poll_handle;

  connection->connection_group->connection_group_state = ct_create_quic_group_state();
  if (!connection->connection_group->connection_group_state) {
    log_error("Failed to allocate QUIC group state");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  connection->internal_connection_state = ct_quic_stream_state_new();
  if (!connection->internal_connection_state) {
    log_error("Failed to allocate QUIC stream state");
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  int rc = resolve_local_endpoint_from_poll(poll_handle, connection);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  size_t alpn_count = 0;
  const char** alpn_strings = ct_security_parameters_get_alpns(connection->security_parameters, &alpn_count);
  if (alpn_count == 0) {
    log_error("No ALPN strings configured for QUIC connection");
    free(connection->connection_group->connection_group_state);
    free(connection->internal_connection_state);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }

  ct_quic_connection_group_state_t* group_state = (ct_quic_connection_group_state_t*)connection->connection_group->connection_group_state;
  group_state->picoquic_connection = picoquic_create_cnx(
    quic_context->picoquic_ctx,
    picoquic_null_connection_id,
    picoquic_null_connection_id,
    (struct sockaddr*) &ct_connection_get_active_remote_endpoint(connection)->data.resolved_address,
    current_time,
    1,
    ct_security_parameters_get_server_name_identification(connection->security_parameters),
    alpn_strings[0], // We create separate candidates for each ALPN to support 0-rtt (see candidate gathering code)
    1
  );
  log_trace("Setting callback context to connection group: %p", (void*)connection->connection_group);

  // Set picoquic callback to connection group
  picoquic_set_callback(group_state->picoquic_connection, picoquic_callback, connection->connection_group);

  rc = picoquic_start_client_cnx(group_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    connection->socket_manager->callbacks.aborted_connection(connection);
    return 0;
  }


  reset_quic_timer(quic_context);
  log_trace("Successfully initiated standalone QUIC connection %p", (void*)connection);
  return 0;
}

int quic_close_connection(ct_connection_t* connection) {
    log_debug("Closing QUIC connection: %s", connection->uuid);
    ct_quic_socket_state_t* socket_state = ct_connection_get_quic_socket_state(connection);
    ct_quic_connection_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
    ct_connection_group_t* connection_group = connection->connection_group;

    ct_connection_set_can_send(connection, false);

    size_t num_active = 0;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, connection_group->connections);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      ct_connection_t* conn = (ct_connection_t*)value;
      if (ct_connection_can_send(conn) || ct_connection_can_receive(conn)) {
        log_debug("Connection %s is still active (can_send=%d, can_receive=%d)", conn->uuid, ct_connection_can_send(conn), ct_connection_can_receive(conn));
        num_active++;
      }
    }
    int rc = 0;


    if (num_active > 0) {
        log_debug("QUIC connection has %zu active streams remaining", num_active);
        if (ct_connection_stream_is_initialized(connection)) {
            log_debug("Sending FIN on stream for connection %s", connection->uuid);
            uint64_t stream_id = ct_connection_get_stream_id(connection);
            rc = picoquic_add_to_stream_with_ctx(group_state->picoquic_connection, stream_id, NULL, 0, 1, connection);
      

            // Force immediate packet preparation and sending
            ct_quic_socket_state_t* socket_state = connection->socket_manager->internal_socket_manager_state;
            on_socket_timer(socket_state->timer_handle);
        }
    } else {
        log_debug("No more active connections in group, closing entire QUIC connection");
        rc = picoquic_close(group_state->picoquic_connection, 0);
    }

    reset_quic_timer(socket_state);

    if (rc != 0) {
        log_error("Error closing QUIC connection: %d", rc);
        return -EIO;
    }
    return 0;
}


void quic_abort(ct_connection_t* connection) {
  ct_quic_connection_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
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
        return;
      }
      ct_connection_mark_as_closed(connection);
    }
    else {
      log_debug("Stream %llu not initialized, no RST sent", (unsigned long long)stream_id);
      return;
    }
  } else {
    log_info("Last active connection in group, closing entire QUIC connection");
    // Marking as closed etc. is handled in callback
    ct_quic_connection_group_set_close_initiated(connection_group, true);
    picoquic_close_immediate(group_state->picoquic_connection);
  }
  reset_quic_timer((ct_quic_socket_state_t*)connection->socket_manager->internal_socket_manager_state);
}

int quic_clone_connection(const struct ct_connection_s* source_connection, struct ct_connection_s* target_connection) {
  log_debug("Creating clone of QUIC connection using multistreaming");
  target_connection->internal_connection_state = calloc(1, sizeof(ct_quic_stream_state_t));
  if (!target_connection->internal_connection_state) {
    log_error("Failed to allocate memory for cloned connection stream state");
    return -ENOMEM;
  }
  ct_socket_manager_t* socket_manager = source_connection->socket_manager;
  socket_manager->callbacks.connection_ready(target_connection);
  return 0;
}


int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context) {
  log_debug("Sending message over QUIC");
  ct_socket_manager_t* socket_manager = connection->socket_manager;
  picoquic_cnx_t* cnx = ct_connection_get_picoquic_connection(connection);

  if (!cnx) {
    log_error("No picoquic connection available for sending");
    return -ENOTCONN;
  }

  // Check if connection is ready to send data
  if (picoquic_get_cnx_state(cnx) < picoquic_state_ready) {
    log_warn("ct_connection_t not ready to send data, state: %d", picoquic_get_cnx_state(cnx));
    return -EAGAIN;
  }

  if (!ct_connection_stream_is_initialized(connection)) {
    log_debug("First message sent on QUIC stream for connection %s, initializing stream", connection->uuid);
    // Determine stream ID based on connection role (client/server) and stream type (bidirectional/unidirectional)
    ct_connection_assign_next_free_stream(connection, false);
  }

  // Add data to the stream (set_fin=0 since we're not closing the stream)
  uint64_t stream_id = ct_connection_get_stream_id(connection);
  log_debug("Queuing %zu bytes for QUIC, sending on stream %llu, connection: %s", message->length, (unsigned long long)stream_id, connection->uuid);

  int set_fin = 0;
  if (message_context && ct_message_properties_get_final(&message_context->message_properties)) {
    log_debug("Setting FIN on QUIC stream %llu for connection: %s", (unsigned long long)stream_id, connection->uuid);
    set_fin = 1;
  }

  int rc = picoquic_add_to_stream_with_ctx(cnx, stream_id, (const uint8_t*)message->content, message->length, set_fin, connection);

  if (rc != 0) {
    log_error("Error queuing data to QUIC stream: %d", rc);
    if (rc == PICOQUIC_ERROR_INVALID_STREAM_ID) {
      log_error("Invalid stream ID: %llu", (unsigned long long)stream_id);
    }
    return -EIO;
  }

  // picoquic_add_to_stream copies the data internally, so we can free the message now
  // message context is freed in the socket manager after the callback
  ct_message_free(message);

  // Reset the timer to ensure data gets processed and sent immediately
  ct_quic_socket_state_t* quic_context = ct_connection_get_quic_socket_state(connection);
  reset_quic_timer(quic_context);

  socket_manager->callbacks.message_sent(connection, message_context);

  return 0;
}

int quic_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Starting QUIC listen");
  ct_listener_t* listener = socket_manager->listener;

  // Get certificate from listener's security parameters
  if (!listener->security_parameters) {
    log_error("Security parameters required for QUIC listener");
    return -EINVAL;
  }

  size_t bundle_count = ct_security_parameters_get_server_certificate_count(listener->security_parameters);

  if (bundle_count == 0) {
    log_error("No certificate bundle configured for QUIC listener");
    return -EINVAL;
  }

  const char* cert_file = ct_security_parameters_get_server_certificate_file(listener->security_parameters, 0);
  const char* key_file = ct_security_parameters_get_server_certificate_key_file(listener->security_parameters, 0);

  if (!cert_file || !key_file) {
    log_error("Certificate or key file not configured in listener security parameters");
    return -EINVAL;
  }

  // Create QUIC context for this listener
  ct_quic_socket_state_t* socket_state = ct_quic_socket_state_new(
    cert_file,
    key_file,
    listener->socket_manager,
    listener->security_parameters,
    NULL
  );

  if (!socket_state) {
    log_error("Failed to create QUIC context for listener");
    return -EIO;
  }

  // Set ALPN select callback
  picoquic_set_alpn_select_fn(socket_state->picoquic_ctx, quic_alpn_select_cb);

  // Create UDP handle bound to the listener's local endpoint
  ct_udp_poll_handle_t* poll_handle = create_udp_poll_on_local(listener->local_endpoint);
  if (!poll_handle) {
    log_error("Failed to create UDP handle for QUIC listener");
    ct_close_quic_context(socket_state);
    return -EIO;
  }

  // Link UDP handle and socket state
  poll_handle->poll.data = socket_manager;
  socket_state->poll_handle = poll_handle;

  socket_manager->internal_socket_manager_state = socket_state;

  // Start the QUIC timer for packet processing
  reset_quic_timer(socket_state);

  return 0;
}

void quic_close_listener(ct_socket_manager_t* socket_manager) {
  log_debug("Stopping QUIC listen");
  // no-op since the socket is shared between listener and connections
  // The socket is instead closed when the socket manager sees no
  // more open connections
  socket_manager->callbacks.closed_listener(socket_manager);
}

void quic_free_state(ct_connection_t *connection) {
  if (!connection || !connection->internal_connection_state) {
    return;
  }
  ct_quic_stream_state_t* stream_state = (ct_quic_stream_state_t*)connection->internal_connection_state;
  free(stream_state);
}

ct_quic_stream_state_t* ct_quic_stream_state_new(void) {
  ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
  if (!stream_state) {
    log_error("Failed to allocate memory for QUIC stream state");
    return NULL;
  }
  memset(stream_state, 0, sizeof(ct_quic_stream_state_t));
  return stream_state;
}

void ct_free_quic_connection_group_state(ct_connection_group_t* connection_group) {
  ct_quic_connection_group_state_t* group_state = (ct_quic_connection_group_state_t*)connection_group->connection_group_state;
  if (!group_state) {
    log_warn("QUIC group state is NULL in close function");
    return;
  }
  free(group_state);
}

void quic_close_socket(ct_socket_manager_t* socket_manager) {
  log_debug("Closing QUIC socket for socket manager %p", socket_manager);
  ct_quic_socket_state_t* socket_state = socket_manager->internal_socket_manager_state;
  ct_close_quic_context(socket_state);
}

void quic_close_connection_group(ct_connection_group_t* connection_group) {
  log_debug("Closing connection group: %s", connection_group->connection_group_id);
  ct_quic_connection_group_state_t* group_state = (ct_quic_connection_group_state_t*)connection_group->connection_group_state;
  int rc = picoquic_close(group_state->picoquic_connection, 0);
  // This only fires if there is an error in our state machine
  assert(rc == 0);
}

int quic_set_connection_priority(ct_connection_t* connection, uint8_t priority) {
  assert(connection && connection->internal_connection_state);
  picoquic_cnx_t* cnx = ct_connection_get_picoquic_connection(connection);
  ct_quic_stream_state_t* stream_state = (ct_quic_stream_state_t*)connection->internal_connection_state;
  if (!stream_state->stream_initialized) {
    log_warn("Attempting to set QUIC stream priority before stream is initialized for connection %s", connection->uuid);
    return -EINVAL;
  }

  int rc = picoquic_set_stream_priority(cnx, stream_state->stream_id, priority);
  if (rc != 0) {
    log_error("Error setting QUIC stream priority: %d", rc);
    return -EIO;
  }
  return 0;
}

void quic_free_socket_state(struct ct_socket_manager_s* socket_manager) {
  ct_quic_socket_state_t* socket_state = (ct_quic_socket_state_t*)socket_manager->internal_socket_manager_state;
  log_debug("Freeing QUIC socket state");
  if (socket_state) {
    log_debug("Freeing QUIC socket state resources");
    picoquic_free(socket_state->picoquic_ctx);
    free(socket_state->poll_handle);
    free(socket_state->timer_handle);
    if (socket_state->cert_file_name) {
      free(socket_state->cert_file_name);
    }
    if (socket_state->ticket_store_path) {
      free(socket_state->ticket_store_path);
    }
    if (socket_state->key_file_name) {
      free(socket_state->key_file_name);
    }
    free(socket_state);
  }
}
