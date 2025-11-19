#include "quic.h"

#include <logging/log.h>
#include <picotls.h>
#include "ctaps.h"
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "ctaps.h"
#include "connection/socket_manager/socket_manager.h"
#include "protocol/common/socket_utils.h"
#include "uv.h"
#include <picoquic.h>
#include <picoquic_utils.h>

#define PICOQUIC_GET_REMOTE_ADDR 2
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


typedef struct ct_quic_connection_state_t {
  uv_udp_t* udp_handle;
  struct sockaddr_storage* udp_sock_name;
  picoquic_cnx_t* picoquic_connection;
} ct_quic_connection_state_t;

void free_quic_connection_state(ct_quic_connection_state_t* state) {
  if (state) {
    if (state->udp_sock_name) {
      free(state->udp_sock_name);
    }
    free(state);
  }
}

static picoquic_quic_t* global_quic_ctx;

static ct_quic_global_state_t default_global_quic_state = {
  .timer_handle = NULL,
  .listener = NULL,
  .num_active_sockets = 0
};

size_t quic_alpn_select_cb(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count) {
  log_trace("QUIC server alpn select cb");

  size_t ret = count;

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
  log_debug("Getting global QUIC context");
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
  log_trace("Resetting QUIC timer to fire in %llu ms", (unsigned long long)MICRO_TO_MILLI(next_wake_delay));
  uv_timer_start(default_global_quic_state.timer_handle, on_quic_timer, MICRO_TO_MILLI(next_wake_delay), 0);
}

void quic_closed_udp_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle for QUIC connection");
  free(handle);
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
  int rc;
  ct_quic_connection_state_t* connection_state = (ct_quic_connection_state_t*)connection->protocol_state;
  if (connection->open_type == CONNECTION_TYPE_STANDALONE) {
    log_info("Closing standalone QUIC connection with UDP handle: %p", connection_state->udp_handle);

    rc = uv_udp_recv_stop(connection_state->udp_handle);
    if (rc < 0) {
      log_error("Error closing underlying QUIC handles: %d", rc);
      return rc;
    }
    uv_close((uv_handle_t*)connection_state->udp_handle, quic_closed_udp_handle_cb);
    log_info("Successfully handled closed QUIC connection");
    free_quic_connection_state(connection_state);
    connection->protocol_state = NULL;
  }
  else if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
    log_info("Removing closed QUIC connection from socket manager");
    rc = socket_manager_remove_connection(connection->socket_manager, connection);


    if (rc < 0) {
      log_error("Error removing closed QUIC connection from socket manager: %d", rc);
      return rc;
    }
    log_info("Successfully removed closed QUIC connection from socket manager");
  }
  else {
    log_error("Unknown connection open type when handling closed QUIC connection");
    return -EINVAL;
  }
  connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;
  reset_quic_timer();
  return 0;
}


int close_timer_handle() {
  log_info("Closing QUIC timer handle");
  int rc = uv_timer_stop(default_global_quic_state.timer_handle);
  if (rc < 0) {
    log_error("Error stopping QUIC timer: %s", uv_strerror(rc));
  }
  uv_close((uv_handle_t*) default_global_quic_state.timer_handle, quic_closed_udp_handle_cb);
  return 0;
}

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
  int rc;
  ct_connection_t* connection = NULL;
  log_trace("ct_callback_t event with connection: %p", (void*)cnx);
  log_trace("Received sample callback event: %d", fin_or_event);

  connection = (ct_connection_t*)callback_ctx;
  ct_quic_connection_state_t* quic_state = (ct_quic_connection_state_t*)connection->protocol_state;
  log_debug("ct_connection_t state is: %d", picoquic_get_cnx_state(quic_state->picoquic_connection));
  switch (fin_or_event) {
    case picoquic_callback_ready:
      log_debug("QUIC connection is ready, invoking CTaps callback");
      if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
        log_debug("ct_connection_t is multiplexed, no need to increment active connection counter");
        ct_listener_t* listener = connection->socket_manager->listener;
        listener->listener_callbacks.connection_received(listener, connection);
      } 
      else if (connection->open_type == CONNECTION_TYPE_STANDALONE) {
        log_debug("ct_connection_t is standalone, incrementing active connection counter");
        connection->connection_callbacks.ready(connection);
      }
      else {
        log_error("Unknown connection open type in picoquic ready callback");
      }
      break;
    case picoquic_callback_stream_data:
      log_debug("Received %zu bytes on stream %d", length, stream_id);
      // Delegate to connection receive handler (handles framing if present)
      ct_connection_on_protocol_receive(connection, bytes, length);
      break;
    case picoquic_callback_stream_fin:
      log_debug("Picoquic stream fin on stream %d", stream_id);
      // Handle stream FIN
      break;
    case picoquic_callback_close:
      log_info("Picoquic callback closed");
      handle_closed_picoquic_connection(connection);

      // Only decrement counter for standalone connections that own their UDP socket
      // Multiplexed connections share the listener's socket and don't affect the counter
      if (connection->open_type == CONNECTION_TYPE_STANDALONE) {
        uint32_t active_sockets = decrement_active_socket_counter();
        if (active_sockets == 0) {
          log_info("No active QUIC sockets remaining, closing timer handle");
          close_timer_handle();
        }
      }
      break;
    case picoquic_callback_application_close:
      log_info("picoquic application closed by peer");
      // Handle application close
      break;
    case picoquic_callback_request_alpn_list:
      log_debug("Picoquic requested ALPN list");
      log_debug("ct_connection_t type is: %d", connection->open_type);
      if (!connection->security_parameters) {
        log_error("No security parameters set for connection when handling ALPN request");
        return -EINVAL;
      }
      const ct_string_array_value_t* alpn_string_array = &connection->security_parameters->security_parameters[ALPN].value.array_of_strings;
      log_trace("Number of ALPN strings to propose: %zu", alpn_string_array->num_strings);
      for (size_t i = 0; i < alpn_string_array->num_strings; i++) {
        picoquic_add_proposed_alpn(bytes, alpn_string_array->strings[i]);
      }
      break;
    default:
      log_debug("Unhandled callback event: %d", fin_or_event);
      break;
  }
  return 0;
}

static void alloc_quic_buf(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
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
  log_debug("Received QUIC message over UDP");
  if (nread < 0) {
    log_error("Read error: %s\n", uv_strerror(nread));
    uv_close((uv_handle_t*)udp_handle, NULL);
    free(buf->base);
    return;
  }

  if (addr_from == NULL) {
    log_warn("No source address for incoming QUIC packet");
    free(buf->base);
    // No more data to read, or an empty packet.
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
    buf->base,
    nread,
    addr_from,
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

    bool was_new = false;
    ct_connection_t* connection = socket_manager_get_or_create_connection(listener->socket_manager, (struct sockaddr_storage*)addr_from, &was_new);

    log_trace("Created new ct_connection_t object for received QUIC cnx: %p", (void*)connection);
    picoquic_set_callback(cnx, picoquic_callback, connection);

    ct_quic_connection_state_t* quic_state = malloc(sizeof(ct_quic_connection_state_t));
    if (!quic_state) {
      log_error("Failed to allocate memory for QUIC connection state");
      free(connection);
      return;
    }
    quic_state->picoquic_connection = cnx;

    log_trace("Setting up received ct_connection_t state for new ct_connection_t");
    ct_quic_connection_state_t* listener_state = (ct_quic_connection_state_t*)listener->socket_manager->protocol_state;
    quic_state->udp_handle = listener_state->udp_handle;
    quic_state->udp_sock_name = malloc(sizeof(struct sockaddr_storage));
    int namelen = sizeof(struct sockaddr_storage);
    rc = uv_udp_getsockname(quic_state->udp_handle, (struct sockaddr*)quic_state->udp_sock_name, &namelen);
    if (rc < 0) {
      log_error("Could not get UDP socket name for QUIC connection: %s", uv_strerror(rc));
      free(quic_state->udp_sock_name);
      free(quic_state);
      free(connection);
      return;
    }

    connection->protocol_state = quic_state;
    log_trace("Done setting up received QUIC connection state");
  }


  log_trace("Processed incoming QUIC packet, picoquic connection: %p", (void*)cnx);

  reset_quic_timer();
}

void on_quic_timer(uv_timer_t* timer_handle) {
  log_debug("QUIC timer triggered, preparing packets to send");

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  size_t send_length = 0;

  struct sockaddr_storage from_address;
  struct sockaddr_storage to_address;
  int if_index = -1;
  picoquic_cnx_t* last_cnx = NULL;

  do {
    send_length = 0;
    log_debug("Preparing next QUIC packet");

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

      ct_connection_t* connection = (ct_connection_t*)picoquic_get_callback_context(last_cnx);
      ct_quic_connection_state_t* quic_state = connection->protocol_state;

      uv_udp_t* udp_handle = quic_state->udp_handle;

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

      log_trace("Sending QUIC data over UDP handle: %p", (void*)udp_handle);
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
      // No data to send, free the buffer
      free(send_buffer_base);
    }
  } while (send_length > 0);
  log_debug("Finished sending QUIC packets");

  reset_quic_timer();
}

uv_timer_t* set_up_timer_handle() {
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
  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  if (!quic_ctx) {
    log_error("Failed to get global QUIC context");
    return -EIO;
  }
  picoquic_cnx_t *cnx = NULL;
  int client_socket = -1;
  uint64_t current_time = 0;
  int ret = 0;

  /* 1. INITIALIZATION & CONTEXT SETUP */

  current_time = picoquic_get_quic_time(quic_ctx);

  uv_udp_t* udp_handle = create_udp_listening_on_local(&connection->local_endpoint, alloc_quic_buf, on_quic_udp_read);
  if (!udp_handle) {
    log_error("Failed to create UDP handle for QUIC connection");
    return -EIO;
  }

  if (default_global_quic_state.timer_handle == NULL) {
    default_global_quic_state.timer_handle = set_up_timer_handle();
  }

  ct_quic_connection_state_t* connection_state = malloc(sizeof(ct_quic_connection_state_t));

  *connection_state = (ct_quic_connection_state_t){
    .udp_handle = udp_handle,
    .udp_sock_name = malloc(sizeof(struct sockaddr_storage)),
  };
  connection->protocol_state = connection_state;

  int namelen = sizeof(struct sockaddr_storage);
  int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)connection_state->udp_sock_name, &namelen);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    free(connection_state->udp_sock_name);
    free(connection_state);
    return rc;
  }

  log_debug("Creating picoquic cnx to remote endpoint");
  connection_state->picoquic_connection = picoquic_create_cnx(
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

  log_trace("ct_connection_t object associated with picoquic cnx: %p", (void*)connection);

  picoquic_set_callback(connection_state->picoquic_connection, picoquic_callback, connection);

  rc = picoquic_start_client_cnx(connection_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    free(connection_state->udp_sock_name);
    free(connection_state);
    return rc;
  }
  increment_active_socket_counter();

  reset_quic_timer();
  log_trace("Successfully initiated standalone QUIC connection %p", (void*)connection);
  return 0;
}

int quic_close(const ct_connection_t* connection) {
  int rc = 0;
  ct_quic_connection_state_t* quic_state = (ct_quic_connection_state_t*)connection->protocol_state;
  log_info("Initiating closing of picoquic connection");
  rc = picoquic_close(quic_state->picoquic_connection, 0);
  if (rc != 0) {
    log_error("Error closing picoquic connection: %d", rc);
  }
  reset_quic_timer();

  return rc;
}


int quic_send(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* ctx) {
  log_debug("Sending message over QUIC");
  ct_quic_connection_state_t* quic_state = (ct_quic_connection_state_t*)connection->protocol_state;
  picoquic_cnx_t* cnx = quic_state->picoquic_connection;

  if (!cnx) {
    log_error("No picoquic connection available for sending");
    return -ENOTCONN;
  }

  // Check if connection is ready to send data
  if (picoquic_get_cnx_state(cnx) < picoquic_state_ready) {
    log_warn("ct_connection_t not ready to send data, state: %d", picoquic_get_cnx_state(cnx));
    return -EAGAIN;
  }

  // Using stream 0 (default bidirectional stream)
  uint64_t stream_id = 0;

  log_debug("Queuing %zu bytes for sending on stream %llu", message->length, (unsigned long long)stream_id);

  // Add data to the stream (set_fin=0 since we're not closing the stream)
  int rc = picoquic_add_to_stream(cnx, stream_id, message->content, message->length, 0);

  if (rc != 0) {
    log_error("Error queuing data to QUIC stream: %d", rc);
    if (rc == PICOQUIC_ERROR_INVALID_STREAM_ID) {
      log_error("Invalid stream ID: %llu", (unsigned long long)stream_id);
    }
    return -EIO;
  }

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

  ct_quic_connection_state_t* listener_state = malloc(sizeof(ct_quic_connection_state_t));
  memset(listener_state, 0, sizeof(ct_quic_connection_state_t));
  ct_local_endpoint_t local_endpoint = ct_listener_get_local_endpoint(socket_manager->listener);

  listener_state->udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_quic_buf, on_quic_udp_read);

  if (!listener_state->udp_handle) {
    free(listener_state);
    return -EIO;
  }

  socket_manager->protocol_state = listener_state;
  socket_manager_increment_ref(socket_manager);
  increment_active_socket_counter();

  default_global_quic_state.listener = socket_manager->listener;

  return 0;
}

int quic_stop_listen(ct_socket_manager_t* socket_manager) {
  log_debug("Stopping QUIC listen");
  ct_quic_connection_state_t* quic_state = (ct_quic_connection_state_t*)socket_manager->protocol_state;
  log_trace("Stopping receive on UDP handle: %p", quic_state->udp_handle);
  // TODO - write test for receive data on a created ct_connection_t after closing listener
  int rc = uv_udp_recv_stop(quic_state->udp_handle);
  if (rc < 0) {
    log_error("Problem with stopping receive: %s\n", uv_strerror(rc));
    return rc;
  }
  uv_close((uv_handle_t*)quic_state->udp_handle, quic_closed_udp_handle_cb);
  free_quic_connection_state(quic_state);

  uint32_t active_sockets = decrement_active_socket_counter();
  if (active_sockets == 0) {
    log_info("No active QUIC sockets remaining after stopping listener, closing timer handle");
    close_timer_handle();
  }

  return 0;
}

int quic_remote_endpoint_from_peer(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer) {
  return -ENOSYS;
}

void quic_retarget_protocol_connection(ct_connection_t* from_connection, ct_connection_t* to_connection) {
  // For QUIC, protocol_state is a ct_quic_connection_state_t that contains:
  // - A UDP handle whose data pointer needs updating
  // - A picoquic connection whose callback context needs updating
  if (from_connection->protocol_state) {
    ct_quic_connection_state_t* quic_state = (ct_quic_connection_state_t*)from_connection->protocol_state;

    // Update the UDP handle's data pointer
    if (quic_state->udp_handle) {
      quic_state->udp_handle->data = to_connection;
    }

    // Update picoquic connection's callback context
    if (quic_state->picoquic_connection) {
      picoquic_set_callback(quic_state->picoquic_connection, picoquic_callback, to_connection);
    }
  }
}
