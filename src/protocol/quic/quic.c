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

void on_quic_timer(uv_timer_t* timer_handle);

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);

typedef struct ct_quic_global_state_t {
  struct ct_listener_s* listener;
  uv_timer_t* timer_handle;
  uint32_t num_active_sockets;
} ct_quic_global_state_t;

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
    if (state->udp_sock_name) {
      free(state->udp_sock_name);
    }
    free(state);
  }
}

void ct_free_quic_stream_state(ct_quic_stream_state_t* state) {
  if (state) {
    free(state);
  }
}

// Forward declarations of helper functions
bool ct_connection_stream_is_initialized(ct_connection_t* connection);
void ct_quic_set_connection_stream(ct_connection_t* connection, uint64_t stream_id);
void ct_connection_assign_next_free_stream(ct_connection_t* connection, bool is_unidirectional);
uint64_t ct_connection_get_stream_id(const ct_connection_t* connection);
ct_quic_group_state_t* ct_connection_get_quic_group_state(const ct_connection_t* connection);
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

static picoquic_quic_t* global_quic_ctx;

static ct_quic_global_state_t default_global_quic_state = {
  .timer_handle = NULL,
  .listener = NULL,
  .num_active_sockets = 0
};

size_t quic_alpn_select_cb(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count) {
  log_trace("QUIC server alpn select cb");

  (void)count;

  /*
   * As far as I can see there is no way of associating this callback with
   * which ct_listener_t is being connected to. This means that if two listeners
   * exist and have different ALPN possibilities, we don't know which one
   * we should check. We can therefore do several things:
   *
   *  - picoquic_quic_t* struct per-listener instead of a single one
   *  - Accept all ALPNs associated with any listener and then redirect
   *  - Dissalow different ALPNs between listeners
   */
  ct_quic_global_state_t* global_state = picoquic_get_default_callback_context(quic);

  ct_listener_t* listener = global_state->listener;

  const ct_string_array_value_t* listener_alpns = &listener->security_parameters->security_parameters[ALPN].value.array_of_strings;

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

picoquic_quic_t* get_global_quic_ctx() {
  log_trace("Getting global QUIC context");
  if (global_quic_ctx == NULL) {
    log_debug("Initializing global QUIC context");
    if (global_config.cert_file_name == NULL) {
      log_error("QUIC global context initialization failed: certificate file not provided");
      return NULL;
    }
    if (global_config.key_file_name == NULL) {
      log_error("QUIC global context initialization failed: key file not provided");
      return NULL;
    }

    global_quic_ctx = picoquic_create(
       MAX_CONCURRENT_QUIC_CONNECTIONS,
       global_config.cert_file_name,
       global_config.key_file_name,
       NULL,
       NULL, // Must set this to NULL, to have callback decide ALPN selection
       picoquic_callback,
       &default_global_quic_state,
       NULL,
       NULL,
       NULL,
       picoquic_current_time(),
       NULL,
       NULL,
       NULL,
      0
      );
    if (!global_quic_ctx) {
      log_error("Failed to create global picoquic context");
      return NULL;
    }
    picoquic_set_alpn_select_fn(global_quic_ctx, quic_alpn_select_cb);
  }
  return global_quic_ctx;
}

void reset_quic_timer() {
  uint64_t next_wake_delay = picoquic_get_next_wake_delay(get_global_quic_ctx(), picoquic_get_quic_time(get_global_quic_ctx()), INT64_MAX - 1);
  log_debug("Resetting QUIC timer to fire in %llu ms", (unsigned long long)MICRO_TO_MILLI(next_wake_delay));
  uv_timer_start(default_global_quic_state.timer_handle, on_quic_timer, MICRO_TO_MILLI(next_wake_delay), 0);
}

void quic_closed_udp_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle for QUIC connection");
  free(handle);
}

void quic_closed_timer_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed QUIC timer handle");
  free(handle);
  default_global_quic_state.timer_handle = NULL;
}

void increment_active_socket_counter() {
  default_global_quic_state.num_active_sockets++;
  log_trace("Active QUIC sockets increased to %u", default_global_quic_state.num_active_sockets);
}

uint32_t decrement_active_socket_counter() {
  if (default_global_quic_state.num_active_sockets > 0) {
    default_global_quic_state.num_active_sockets--;
  }
  log_trace("Active QUIC sockets decreased to %u", default_global_quic_state.num_active_sockets);
  return default_global_quic_state.num_active_sockets;
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
    uv_close((uv_handle_t*)group_state->udp_handle, quic_closed_udp_handle_cb);
    log_info("Successfully handled closed QUIC connection");
    ct_free_quic_group_state(group_state);
    ct_free_quic_stream_state(stream_state);
    connection->connection_group->connection_group_state = NULL;
    connection->internal_connection_state = NULL;
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
  log_info("Setting connection state to CLOSED for connection: %p", (void*)connection);
  ct_connection_mark_as_closed(connection);
  reset_quic_timer();
  return 0;
}


int close_timer_handle() {
  log_info("Closing QUIC timer handle");
  int rc = uv_timer_stop(default_global_quic_state.timer_handle);
  if (rc < 0) {
    log_error("Error stopping QUIC timer: %s", uv_strerror(rc));
  }
  uv_close((uv_handle_t*) default_global_quic_state.timer_handle, quic_closed_timer_handle_cb);
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
  log_debug("QUIC connection state is: %d", picoquic_get_cnx_state(group_state->picoquic_connection));
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

      if (ct_connection_is_server(connection)) {
        log_debug("Server connection ready, notifying listener");
        ct_listener_t* listener = connection->socket_manager->listener;
        ct_connection_mark_as_established(connection);
        listener->listener_callbacks.connection_received(listener, connection);
      }
      else if (ct_connection_is_client(connection)) {
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
              listener->listener_callbacks.connection_received(listener, new_stream_connection);
            } else {
              log_warn("Received new stream but listener has been closed, not notifying application");
            }

            // Use helper function to handle received data
            return handle_stream_data(new_stream_connection, bytes, length);
          } if (ct_connection_is_client(first_connection)) {
            log_error("Received new remote-initiated stream on client connection - multi-streaming not yet implemented");
            return -ENOSYS;
          } else {
            log_error("Unknown connection role when handling new remote-initiated stream");
            return -EINVAL;
          }
        } else {
          log_debug("First connection has uninitialized stream, using it for stream %llu", (unsigned long long)stream_id);
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
      log_info("Picoquic callback closed");
      // When the QUIC connection closes, all streams are closed
      {
        uint32_t size = g_hash_table_size(connection_group->connections);
        log_info("Number of connections in group at close event: %d", size);
        log_info("Active connections before close: %u", connection_group->num_active_connections);

        // Mark all connections in the group as closed
        GHashTableIter iter;
        gpointer key = NULL;
        gpointer value = NULL;
        g_hash_table_iter_init(&iter, connection_group->connections);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
          ct_connection_t* conn = (ct_connection_t*)value;
          ct_connection_mark_as_closed(conn);
        }

        // Reset the active connection counter since entire QUIC connection is closed
        connection_group->num_active_connections = 0;

        // Get first connection to determine type and handle cleanup
        connection = ct_connection_group_get_first(connection_group);
        if (connection) {
          log_info("Picoquic close callback for connection: %p", (void*)connection);
          handle_closed_picoquic_connection(connection);

          // Only decrement counter for standalone connections that own their UDP socket
          // Multiplexed connections share the listener's socket and don't affect the counter
          if (connection->socket_type == CONNECTION_SOCKET_TYPE_STANDALONE) {
            uint32_t active_sockets = decrement_active_socket_counter();
            if (active_sockets == 0) {
              log_info("No active QUIC sockets remaining, closing timer handle");
              close_timer_handle();
            }
          }
        }
      }
      break;
    case picoquic_callback_application_close:
      log_info("picoquic application closed by peer");
      // Handle application close
      break;
    case picoquic_callback_request_alpn_list:
      log_debug("Picoquic requested ALPN list");
      // Get first connection to check security parameters
      {
        connection = ct_connection_group_get_first(connection_group);
        if (!connection) {
          log_error("No connections in group when handling ALPN request");
          return -EINVAL;
        }
        log_debug("ct_connection_t type is: %d", connection->socket_type);
        if (!connection->security_parameters) {
          log_error("No security parameters set for connection when handling ALPN request");
          return -EINVAL;
        }
        const ct_string_array_value_t* alpn_string_array = &connection->security_parameters->security_parameters[ALPN].value.array_of_strings;
        log_trace("Number of ALPN strings to propose: %zu", alpn_string_array->num_strings);
        for (size_t i = 0; i < alpn_string_array->num_strings; i++) {
          picoquic_add_proposed_alpn(bytes, alpn_string_array->strings[i]);
        }
      }
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

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();

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
    quic_ctx,
    (uint8_t*)buf->base,
    nread,
    (struct sockaddr*)addr_from,
    (struct sockaddr*)&addr_to_storage,
    0,
    0,
    &cnx,
    picoquic_get_quic_time(quic_ctx)
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
    ct_listener_t* listener = default_global_quic_state.listener;

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

    log_trace("Setting up received ct_connection_t state for new ct_connection_t");
    ct_quic_group_state_t* listener_group_state = (ct_quic_group_state_t*)listener->socket_manager->internal_socket_manager_state;
    group_state->udp_handle = listener_group_state->udp_handle;
    group_state->udp_sock_name = malloc(sizeof(struct sockaddr_storage));
    int namelen = sizeof(struct sockaddr_storage);
    rc = uv_udp_getsockname(group_state->udp_handle, (struct sockaddr*)group_state->udp_sock_name, &namelen);
    if (rc < 0) {
      log_error("Could not get UDP socket name for QUIC connection: %s", uv_strerror(rc));
      free(group_state->udp_sock_name);
      free(group_state);
      free(connection);
      return;
    }

    connection->connection_group->connection_group_state = group_state;

    // Allocate per-stream state (stream_id will be set when stream is created)
    ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
    if (!stream_state) {
      log_error("Failed to allocate memory for QUIC stream state");
      free(group_state->udp_sock_name);
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

  reset_quic_timer();
}

void on_quic_timer(uv_timer_t* timer_handle) {
  (void)timer_handle;
  log_trace("QUIC timer triggered, checking for new QUIC packets to send");

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
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
      quic_ctx,
      picoquic_get_quic_time(quic_ctx),
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

  reset_quic_timer();
}

uv_timer_t* set_up_timer_handle(void) {
  uv_timer_t* timer_handle = malloc(sizeof(*timer_handle));
  if (timer_handle == NULL) {
      log_error("Failed to allocate memory for timer handle");
      return NULL;
  }

  int rc = uv_timer_init(event_loop, timer_handle);
  if (rc < 0) {
      log_error("Error initializing timer handle: %s", uv_strerror(rc));
      free(timer_handle);
      return NULL;
  }
  return timer_handle;
}

int quic_init(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks) {
  (void)connection_callbacks;
  log_info("Initializing standalone QUIC connection");
  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  if (!quic_ctx) {
    log_error("Failed to get global QUIC context");
    return -EIO;
  }
  uint64_t current_time = 0;

  /* 1. INITIALIZATION & CONTEXT SETUP */

  current_time = picoquic_get_quic_time(quic_ctx);

  uv_udp_t* udp_handle = create_udp_listening_on_local(&connection->local_endpoint, alloc_quic_buf, on_quic_udp_read);
  if (!udp_handle) {
    log_error("Failed to create UDP handle for QUIC connection");
    return -EIO;
  }
  log_debug("Created UDP handle %p for QUIC connection", (void*)udp_handle);

  if (default_global_quic_state.timer_handle == NULL) {
    default_global_quic_state.timer_handle = set_up_timer_handle();
    log_debug("Set up QUIC timer handle: %p", (void*)default_global_quic_state.timer_handle);
  }

  // Allocate shared group state (UDP handle + QUIC connection)
  ct_quic_group_state_t* group_state = ct_create_quic_group_state();
  if (!group_state) {
    log_error("Failed to allocate QUIC group state");
    return -ENOMEM;
  }

  *group_state = (ct_quic_group_state_t){
    .udp_handle = udp_handle,
    .udp_sock_name = malloc(sizeof(struct sockaddr_storage)),
    .picoquic_connection = NULL,
  };
  connection->connection_group->connection_group_state = group_state;

  ct_quic_stream_state_t* stream_state = malloc(sizeof(ct_quic_stream_state_t));
  if (!stream_state) {
    log_error("Failed to allocate QUIC stream state");
    free(group_state->udp_sock_name);
    free(group_state);
    return -ENOMEM;
  }
  stream_state->stream_id = 0;
  stream_state->stream_initialized = false;
  connection->internal_connection_state = stream_state;

  int namelen = sizeof(struct sockaddr_storage);
  int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)group_state->udp_sock_name, &namelen);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    free(group_state->udp_sock_name);
    free(group_state);
    free(stream_state);
    return rc;
  }

  log_debug("Creating picoquic cnx to remote endpoint");
  group_state->picoquic_connection = picoquic_create_cnx(
      quic_ctx,
      picoquic_null_connection_id,
      picoquic_null_connection_id,
      (struct sockaddr*) &connection->remote_endpoint.data.resolved_address,
      current_time,
      1,
      "localhost",
      NULL, // If we set ALPN here we can picoquic only lets us set one, therefore set in callback instead to set potentially multiple
      1
  );

  log_trace("Setting callback context to connection group: %p", (void*)connection->connection_group);

  // Set picoquic callback to connection group
  picoquic_set_callback(group_state->picoquic_connection, picoquic_callback, connection->connection_group);

  rc = picoquic_start_client_cnx(group_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    free(group_state->udp_sock_name);
    free(group_state);
    free(stream_state);
    return rc;
  }
  increment_active_socket_counter();

  reset_quic_timer();
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

  reset_quic_timer();
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
    // Multiple streams active - only close this stream with FIN
    log_info("Multiple active connections in group, closing stream %llu with RST",
             (unsigned long long)stream_id);

    if (ct_connection_stream_is_initialized(connection)) {
      log_debug("Sending RST on stream %llu for connection: %s", (unsigned long long)stream_id, connection->uuid);
      int rc = picoquic_reset_stream(group_state->picoquic_connection, stream_id, 0);
      if (rc != 0) {
        log_error("Error sending FIN on stream %llu: %d", (unsigned long long)stream_id, rc);
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
    picoquic_close_immediate(group_state->picoquic_connection);

    ct_connection_group_decrement_active(connection_group);
  }

  reset_quic_timer();
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
    ct_message_free_all(message);
    return -ENOTCONN;
  }

  // Check if connection is ready to send data
  if (picoquic_get_cnx_state(cnx) < picoquic_state_ready) {
    log_warn("ct_connection_t not ready to send data, state: %d", picoquic_get_cnx_state(cnx));
    ct_message_free_all(message);
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
    ct_message_free_all(message);
    return -EIO;
  }

  // picoquic_add_to_stream copies the data internally, so we can free the message now
  ct_message_free_all(message);

  // Reset the timer to ensure data gets processed and sent immediately
  reset_quic_timer();

  if (connection->connection_callbacks.sent) {
    connection->connection_callbacks.sent(connection);
  }

  return 0;
}

int quic_listen(ct_socket_manager_t* socket_manager) {
  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  if (!quic_ctx) {
    log_error("Failed to get global QUIC context");
    return -EIO;
  }
  if (default_global_quic_state.listener != NULL) {
    log_error("Multiple listeners is currently not supported");
    return -ENOSYS;
  }

  ct_quic_group_state_t* listener_group_state = ct_create_quic_group_state();
  if (!listener_group_state) {
    log_error("Failed to allocate QUIC listener group state");
    return -ENOMEM;
  }
  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(socket_manager->listener);

  listener_group_state->udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_quic_buf, on_quic_udp_read);

  if (!listener_group_state->udp_handle) {
    free(listener_group_state);
    return -EIO;
  }

  socket_manager->internal_socket_manager_state = listener_group_state;
  socket_manager_increment_ref(socket_manager);
  increment_active_socket_counter();

  default_global_quic_state.listener = socket_manager->listener;

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
  ct_free_quic_group_state(group_state);

  uint32_t active_sockets = decrement_active_socket_counter();
  if (active_sockets == 0) {
    log_info("No active QUIC sockets remaining after stopping listener, closing timer handle");
    close_timer_handle();
  }

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
