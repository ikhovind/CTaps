#include "quic.h"

#include <ctaps.h>
#include <logging/log.h>
#include <picotls.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "connections/connection/connection.h"
#include "connections/listener/listener.h"
#include "connections/listener/socket_manager/socket_manager.h"
#include "protocols/common/socket_utils.h"
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

typedef struct QuicGlobalState {
  struct Listener* listener;
  uv_timer_t* timer_handle;
  uint32_t num_active_sockets;
} QuicGlobalState;


typedef struct QuicConnectionState {
  uv_udp_t* udp_handle;
  struct sockaddr_storage* udp_sock_name;
  picoquic_cnx_t* picoquic_connection;
} QuicConnectionState;

static picoquic_quic_t* global_quic_ctx;

static QuicGlobalState default_global_quic_state = {
  .timer_handle = NULL,
  .listener = NULL,
  .num_active_sockets = 0
};

size_t quic_alpn_select_cb(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count) {

  size_t ret = count;

  // TODO - iterate and find compatible ALPN
  for (size_t i = 0; i < count; i++) {
    //log_trace("Base string: %.*s", list[i].base);
  }

  return 0;
}

picoquic_quic_t* get_global_quic_ctx() {
  log_debug("Getting global QUIC context");
  if (global_quic_ctx == NULL) {
    log_debug("Initializing global QUIC context");
    if (ctaps_global_config.cert_file_name == NULL) {
      log_error("QUIC global context initialization failed: certificate file not provided");
      return NULL;
    }
    if (ctaps_global_config.key_file_name == NULL) {
      log_error("QUIC global context initialization failed: key file not provided");
      return NULL;
    }
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    log_debug("Current working directory is: %s", cwd);
    log_debug("Using certificate file: %s", ctaps_global_config.cert_file_name);
    log_debug("Using key file: %s", ctaps_global_config.key_file_name);
    global_quic_ctx = picoquic_create(
       MAX_CONCURRENT_QUIC_CONNECTIONS,
       ctaps_global_config.cert_file_name,
       ctaps_global_config.key_file_name,
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
  log_trace("Resetting QUIC timer to fire in %llu us", (unsigned long long)next_wake_delay);
  uv_timer_start(default_global_quic_state.timer_handle, on_quic_timer, MICRO_TO_MILLI(next_wake_delay), 0);
}

void quic_closed_udp_handle_cb(uv_handle_t* handle) {
  log_info("Successfully closed UDP handle for QUIC connection");
  free(handle);
}

void increment_active_connection_counter() {
  default_global_quic_state.num_active_sockets++;
  log_trace("Active QUIC connections increased to %u", default_global_quic_state.num_active_sockets);
}

uint32_t decrement_active_connection_counter() {
  if (default_global_quic_state.num_active_sockets > 0) {
    default_global_quic_state.num_active_sockets--;
  }
  log_trace("Active QUIC connections decreased to %u", default_global_quic_state.num_active_sockets);
  return default_global_quic_state.num_active_sockets;
}

int handle_closed_quic_connection(Connection* connection) {
  int rc;
  QuicConnectionState* connection_state = (QuicConnectionState*)connection->protocol_state;
  if (connection->open_type == CONNECTION_TYPE_STANDALONE) {
    log_info("Closing standalone QUIC connection with UDP handle: %p", connection_state->udp_handle);

    rc = uv_udp_recv_stop(connection_state->udp_handle);
    if (rc < 0) {
      log_error("Error closing underlying QUIC handles: %d", rc);
      return rc;
    }
    uv_close((uv_handle_t*)connection_state->udp_handle, quic_closed_udp_handle_cb);
    log_info("Successfully handled closed QUIC connection");
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

  log_trace("Setting callback to NULL for %p", (void*)connection_state->picoquic_connection);
  picoquic_set_callback(connection_state->picoquic_connection, NULL, NULL);
  // set connection state to closed
  connection->transport_properties.connection_properties.list[STATE].value.uint32_val = CONN_STATE_CLOSED;
  reset_quic_timer();
  return 0;
}


int close_timer_handle() {
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
  Connection* connection = NULL;
  log_trace("Callback event with connection: %p", (void*)cnx);
  log_trace("Received sample callback event: %d", fin_or_event);

  connection = (Connection*)callback_ctx;
  QuicConnectionState* quic_state = (QuicConnectionState*)connection->protocol_state;
  log_debug("Connection state is: %d", picoquic_get_cnx_state(quic_state->picoquic_connection));
  switch (fin_or_event) {
    case picoquic_callback_ready:
      log_debug("QUIC connection is ready, invoking CTaps callback");
      if (connection->open_type == CONNECTION_OPEN_TYPE_MULTIPLEXED) {
        log_debug("Connection is multiplexed, no need to increment active connection counter");
        Listener* listener = connection->socket_manager->listener;
        listener->listener_callbacks.connection_received(listener, connection, listener->listener_callbacks.user_data);
      } 
      else if (connection->open_type == CONNECTION_TYPE_STANDALONE) {
        log_debug("Connection is standalone, incrementing active connection counter");
        connection->connection_callbacks.ready(connection, connection->connection_callbacks.user_data);
      }
      else {
        log_error("Unknown connection open type in picoquic ready callback");
      }
      break;
    case picoquic_callback_stream_data:
      log_debug("Received %zu bytes on stream %d", length, stream_id);
      // Store received data for later delivery
      Message* msg = malloc(sizeof(Message));
      if (msg) {
        msg->content = malloc(length);
        if (msg->content) {
          memcpy(msg->content, bytes, length);
          msg->length = length;
          if (g_queue_is_empty(connection->received_callbacks)) {
            g_queue_push_tail(connection->received_messages, msg);
          }
          else {
            ReceiveCallbacks* cb = g_queue_pop_head(connection->received_callbacks);
            cb->receive_callback(connection, &msg, NULL, cb->user_data);
            free(cb);
          }
        } else {
          log_error("Failed to allocate memory for received message content");
          free(msg);
        }
      }
      break;
    case picoquic_callback_stream_fin:
      log_debug("Picoquic stream fin on stream %d", stream_id);
      // Handle stream FIN
      break;
    case picoquic_callback_close:
      log_info("Picoquic callback closed");
      handle_closed_quic_connection(connection);

      uint32_t active_conns = decrement_active_connection_counter();
      if (active_conns == 0) {
        log_info("No active QUIC connections remaining closing timer handle");
        close_timer_handle();
      }
      break;
    case picoquic_callback_application_close:
      log_info("picoquic application closed by peer");
      // Handle application close
      break;
    case picoquic_callback_request_alpn_list:
      log_debug("Picoquic requested ALPN list");
      log_debug("Connection type is: %d", connection->open_type);
      if (!connection->security_parameters) {
        log_error("No security parameters set for connection when handling ALPN request");
        return -EINVAL;
      }
      const StringArrayValue* alpn_string_array = &connection->security_parameters->security_parameters[ALPN].value.array_of_strings;
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
  if (rc != 0) {
    log_error("Error processing incoming QUIC packet: %d", rc);
    // TODO - error handling
  }
  
  // If we haven't set the callback context, this means this cnx was just created by picoquic, need to
  // create our own Connection
  if (picoquic_get_callback_context(cnx) == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
    log_info("Received packet for new QUIC cnx for listener");
    Listener* listener = default_global_quic_state.listener;

    if (rc != 0) {
      log_error("Could not get remote address from picoquic connection: %d", rc);
      return;
    }

    bool was_new = false;
    Connection* connection = socket_manager_get_or_create_connection(listener->socket_manager, (struct sockaddr_storage*)addr_from, &was_new);

    log_trace("Created new Connection object for received QUIC cnx: %p", (void*)connection);
    picoquic_set_callback(cnx, picoquic_callback, connection);

    QuicConnectionState* quic_state = malloc(sizeof(QuicConnectionState));
    if (!quic_state) {
      log_error("Failed to allocate memory for QUIC connection state");
      free(connection);
      return;
    }
    quic_state->picoquic_connection = cnx;

    log_trace("Setting up received Connection state for new Connection");
    QuicConnectionState* listener_state = (QuicConnectionState*)listener->socket_manager->protocol_state;
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
  /*
    ##### Flow of a picoquic application according to the picoquic documentation: #####

    1. Create a QUIC context
    2. If running as a client, create the client connection
    3. Initialize the network, for example by opening sockets
    4. Loop:
      * Check how long the QUIC context can wait until the next action, i.e. get a timer t, using the polling API
      * Wait until either the timer t elapses or packets are ready to process
      * Process all the packets that have arrived and submit them through the {{incoming-api}} (This is done on arrival, so not needed here)
      * Poll the QUIC context through the {{prepare-api}} and send packets if they are ready (This has to be done here)
      * If error happen when sending packets, report issues through the {{error-notify-API}} (If applicable done here)
    5. Exit the loop when the client connections are finished, or on a server if the server process needs to close.
    6. Close the QUIC context
   */

  // step 1: Process all the packets that have arrived and submit them through the {{incoming-api}}
  // this is handled through the on_read callback for data
  // Poll the QUIC context through the {{prepare-api}} and send packets if they are ready
  log_debug("QUIC timer triggered, preparing packets to send");
  unsigned char send_buffer_base[MAX_QUIC_PACKET_SIZE];

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  size_t send_length = 0;

  uv_buf_t send_buffer = uv_buf_init(send_buffer_base, MAX_QUIC_PACKET_SIZE);

  struct sockaddr_storage from_address;
  struct sockaddr_storage to_address;
  int if_index = -1;
  picoquic_cnx_t* last_cnx = NULL;

  do {
    send_length = 0;
    log_debug("Preparing next QUIC packet");
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
      // TODO - error handling
      break;
    }

    log_debug("Prepared QUIC packet of length %zu", send_length);
    if (send_length > 0) {

      Connection* connection = (Connection*)picoquic_get_callback_context(last_cnx);
      QuicConnectionState* quic_state = connection->protocol_state;

      uv_udp_t* udp_handle = quic_state->udp_handle;
      // TODO - is it actually correct to modify the buffer directly like this?
      send_buffer.len = send_length;
      uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
      log_trace("Sending QUIC data over UDP handle: %p", (void*)udp_handle);
      rc = uv_udp_send(
        send_req,
        udp_handle,
        &send_buffer,
        1,
        (struct sockaddr*)&to_address,
        on_quic_udp_send);
      if (rc < 0) {
        log_error("Error sending QUIC packet over UDP: %s", uv_strerror(rc));
        free(send_req);
        // TODO - error handling
        break;
      }
      log_debug("Sent QUIC packet of length %zu", send_length);
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

  int rc = uv_timer_init(ctaps_event_loop, timer_handle);
  if (rc < 0) {
      log_error("Error initializing timer handle: %s", uv_strerror(rc));
      free(timer_handle);
      return NULL;
  }
  return timer_handle;
}

int quic_init(Connection* connection, const ConnectionCallbacks* connection_callbacks) {
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

  QuicConnectionState* connection_state = malloc(sizeof(QuicConnectionState));

  *connection_state = (QuicConnectionState){
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

  log_trace("Connection object associated with picoquic cnx: %p", (void*)connection);

  picoquic_set_callback(connection_state->picoquic_connection, picoquic_callback, connection);

  rc = picoquic_start_client_cnx(connection_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    free(connection_state->udp_sock_name);
    free(connection_state);
    return rc;
  }
  increment_active_connection_counter();

  reset_quic_timer();
  log_trace("Successfully initiated standalone QUIC connection %p", (void*)connection);
  return 0;
}

int quic_close(const Connection* connection) {
  int rc = 0;
  QuicConnectionState* quic_state = (QuicConnectionState*)connection->protocol_state;
  log_info("Initiating closing of picoquic connection");
  rc = picoquic_close(quic_state->picoquic_connection, 0);
  if (rc != 0) {
    log_error("Error closing picoquic connection: %d", rc);
  }
  reset_quic_timer();

  return rc;
}


int quic_send(Connection* connection, Message* message, MessageContext* ctx) {
  log_debug("Sending message over QUIC");
  QuicConnectionState* quic_state = (QuicConnectionState*)connection->protocol_state;
  picoquic_cnx_t* cnx = quic_state->picoquic_connection;

  if (!cnx) {
    log_error("No picoquic connection available for sending");
    return -ENOTCONN;
  }

  // Check if connection is ready to send data
  if (picoquic_get_cnx_state(cnx) < picoquic_state_ready) {
    log_warn("Connection not ready to send data, state: %d", picoquic_get_cnx_state(cnx));
    return -EAGAIN;
  }

  // Using stream 0 (default bidirectional stream)
  uint64_t stream_id = 0;

  log_debug("Queuing %zu bytes for sending on stream %llu", message->length, (unsigned long long)stream_id);

  // Add data to the stream (set_fin=0 since we're not closing the stream)
  int rc = picoquic_add_to_stream(cnx, stream_id, message->content, message->length, 0);

  if (rc != 0) {
    log_error("Error queuing data to QUIC stream: %d", rc);
    return -EIO;
  }

  // Reset the timer to ensure data gets processed and sent immediately
  reset_quic_timer();

  // Trigger the sent callback if registered (queuing is synchronous)
  if (connection->connection_callbacks.sent) {
    connection->connection_callbacks.sent(connection, connection->connection_callbacks.user_data);
  }

  return 0;
}



int quic_receive(Connection* connection, ReceiveCallbacks receive_callbacks) {
  log_debug("Attempting to receive message via QUIC");

  // If we have a message to receive then simply return that
  if (!g_queue_is_empty(connection->received_messages)) {
    log_debug("Calling receive callback immediately");
    Message* received_message = g_queue_pop_head(connection->received_messages);
    receive_callbacks.receive_callback(connection, &received_message, NULL, receive_callbacks.user_data);
    return 0;
  }

  log_debug("Pushing receive callback to queue");
  // Allocate memory for the callback structure
  ReceiveCallbacks* ptr = malloc(sizeof(ReceiveCallbacks));
  if (!ptr) {
    log_error("Failed to allocate memory for receive callback");
    return -ENOMEM;
  }
  // Copy the callback structure
  memcpy(ptr, &receive_callbacks, sizeof(ReceiveCallbacks));

  // Add the callback to the queue of waiting callbacks
  g_queue_push_tail(connection->received_callbacks, ptr);
  log_trace("Length of received_callbacks queue after adding: %d", 
           g_queue_get_length(connection->received_callbacks));

  return 0;
}

int quic_listen(SocketManager* socket_manager) {
  int rc;
  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  if (!quic_ctx) {
    log_error("Failed to get global QUIC context");
    return -EIO;
  }
  if (default_global_quic_state.listener != NULL) {
    log_error("QUIC listener already set up for SocketManager %p", (void*)socket_manager);
    return -EALREADY;
  }

  QuicConnectionState* listener_state = malloc(sizeof(QuicConnectionState));
  LocalEndpoint local_endpoint = listener_get_local_endpoint(socket_manager->listener);

  listener_state->udp_handle = create_udp_listening_on_local(&local_endpoint, alloc_quic_buf, on_quic_udp_read);

  if (!listener_state->udp_handle) {
    free(listener_state);
    return -EIO;
  }

  socket_manager->protocol_state = listener_state;
  socket_manager_increment_ref(socket_manager);
  increment_active_connection_counter();

  default_global_quic_state.listener = socket_manager->listener;
  
  return 0;
}

int quic_stop_listen(SocketManager* socket_manager) {
  log_debug("Stopping QUIC listen");
  QuicConnectionState* quic_state = (QuicConnectionState*)socket_manager->protocol_state;
  log_trace("Stopping receive on UDP handle: %p", quic_state->udp_handle);
  int rc = uv_udp_recv_stop(quic_state->udp_handle);
  if (rc < 0) {
    log_error("Problem with stopping receive: %s\n", uv_strerror(rc));
    return rc;
  }
  decrement_active_connection_counter();
  return 0;
}

int quic_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer) {
  return -ENOSYS;
}
