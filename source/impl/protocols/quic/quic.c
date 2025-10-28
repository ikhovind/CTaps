#include "quic.h"

#include <ctaps.h>
#include <logging/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <picoquic.h>
#include "connections/connection/connection.h"
#include "connections/listener/listener.h"
#include "connections/listener/socket_manager/socket_manager.h"
#include "uv.h"

#define PICOQUIC_GET_REMOTE_ADDR 2
#define MAX_QUIC_PACKET_SIZE 1500

#define MICRO_TO_MILLI(us) ((us) / 1000)

#define CONNECTION_FROM_HANDLE(handle) (Connection*)(handle->data)
#define QUIC_STATE_FROM_HANDLE(handle) (QuicConnectionState*)(CONNECTION_FROM_HANDLE(handle))->protocol_state;

void on_quic_timer(uv_timer_t* timer_handle);

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);

typedef struct QuicGlobalState {
  struct Listener* listener;
  uv_timer_t* timer_handle;
} QuicGlobalState;


typedef struct QuicConnectionState {
  uv_udp_t* udp_handle;
  struct sockaddr_storage* udp_sock_name;
  picoquic_cnx_t* picoquic_connection;
} QuicConnectionState;

static picoquic_quic_t* global_quic_ctx;

static QuicGlobalState default_global_quic_state = {
  .timer_handle = NULL,
  .listener = NULL
};

picoquic_quic_t* get_global_quic_ctx() {
  if (global_quic_ctx == NULL) {
    global_quic_ctx = picoquic_create(
       8,
       "/home/ikhovind/Documents/Skole/taps/test/quic/cert.pem",
       "/home/ikhovind/Documents/Skole/taps/test/quic/key.pem",
       NULL,
       "simple-ping",
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


int close_underlying_handles(Connection* connection) {
  int rc;
  rc = uv_timer_stop(default_global_quic_state.timer_handle);
  if (rc < 0) {
    log_error("Error stopping QUIC timer: %s", uv_strerror(rc));
  } 
  uv_close((uv_handle_t*) default_global_quic_state.timer_handle, quic_closed_udp_handle_cb);

  g_queue_free(connection->received_messages);
  g_queue_free(connection->received_callbacks);

  QuicConnectionState* quic_state = (QuicConnectionState*)connection->protocol_state;
  rc = uv_udp_recv_stop(quic_state->udp_handle);
  if (rc < 0) {
    log_error("Problem with stopping QUIC UDP receive: %s\n", uv_strerror(rc));
  }
  uv_close((uv_handle_t*)quic_state->udp_handle, quic_closed_udp_handle_cb);
  
  if (rc != 0) {
    log_error("Error closing picoquic connection: %d", rc);
    return rc;
  }
  return 0;
}

int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
  int rc;
  Connection* connection = NULL;
  log_trace("Received sample callback event: %d", fin_or_event);
  /* If this is the first reference to the connection, the application context is set
   * to the default value defined for the server. TODO - define this lol
   */
  log_trace("Callback context pointer: %p", callback_ctx);
  log_trace("Default callback context pointer: %p", picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx)));
  log_trace("Equal: %d", (callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))));
  if (callback_ctx == NULL || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
    // TODO - invoke listener callback for new connection
    // TODO - avoid invoking default connection ready callback (Or maybe this can be used to signal new connection for listener?)
    log_info("New connection received in QUIC callback");

    Listener* listener = default_global_quic_state.listener;


    struct sockaddr_storage remote_addr;
    rc = picoquic_get_path_addr(cnx, stream_id, PICOQUIC_GET_REMOTE_ADDR, &remote_addr);
    if (rc != 0) {
      log_error("Could not get remote address from picoquic connection: %d", rc);
    }

    bool was_new = false;
    connection = socket_manager_get_connection_from_remote(listener->socket_manager, &remote_addr, &was_new);
    log_trace("Created new Connection object for received QUIC connection: %p", (void*)connection);
    picoquic_set_callback(cnx, picoquic_callback, connection);

    QuicConnectionState* quic_state = malloc(sizeof(QuicConnectionState));
    if (!quic_state) {
      log_error("Failed to allocate memory for QUIC connection state");
      free(connection);
      return -ENOMEM;
    }
    quic_state->picoquic_connection = cnx;

    log_trace("Setting up received QUIC connection state");
    QuicConnectionState* listener_state = (QuicConnectionState*)listener->socket_manager->protocol_state;
    quic_state->udp_handle = listener_state->udp_handle;
    quic_state->udp_sock_name = malloc(sizeof(struct sockaddr_storage));
    int namelen = sizeof(struct sockaddr_storage);
    int rc = uv_udp_getsockname(quic_state->udp_handle, (struct sockaddr*)quic_state->udp_sock_name, &namelen);
    connection->protocol_state = quic_state;
    log_trace("Done setting up received QUIC connection state");

    quic_state->udp_handle->data = connection;
    if (rc < 0) {
      log_error("Could not get UDP socket name for QUIC connection: %s", uv_strerror(rc));
      free(quic_state->udp_sock_name);
      free(quic_state);
      free(connection);
      return rc;
    }
    listener->listener_callbacks.connection_received(listener, connection, listener->listener_callbacks.user_data);
  }
  else {
    connection = (Connection*)callback_ctx;
  }

  QuicConnectionState* quic_state = (QuicConnectionState*)connection->protocol_state;
  log_debug("Connection state is: %d", picoquic_get_cnx_state(quic_state->picoquic_connection));
  switch (fin_or_event) {
    case picoquic_callback_ready:
      log_debug("QUIC connection is ready, invoking CTaps callback");
      if (connection->connection_callbacks.ready != NULL) {
        connection->connection_callbacks.ready(connection, connection->connection_callbacks.user_data);
      } else {
        log_info("No ready callback set for connection");
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
      log_debug("Received FIN on stream %d", stream_id);
      // Handle stream FIN
      break;
    case picoquic_callback_close:
      log_info("Connection closed");
      break;
    case picoquic_callback_application_close:
      log_info("Application closed by peer");
      // Handle application close
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

  rc = picoquic_incoming_packet_ex(
    quic_ctx,
    buf->base,
    nread,
    addr_from,
    &addr_to_storage,
    0,
    0,
    &cnx,
    picoquic_get_quic_time(quic_ctx)
  );
  if (rc != 0) {
    log_error("Error processing incoming QUIC packet: %d", rc);
    // TODO - error handling
  }
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
  QuicConnectionState* quic_state = QUIC_STATE_FROM_HANDLE(timer_handle);
  uv_udp_t* udp_handle = quic_state->udp_handle;

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  size_t send_length = 0;

  uv_buf_t send_buffer = uv_buf_init(send_buffer_base, MAX_QUIC_PACKET_SIZE);

  struct sockaddr_storage from_address;
  struct sockaddr_storage to_address;
  int if_index = -1;
  picoquic_cnx_t* last_cnx = NULL;

  if (picoquic_get_cnx_state(quic_state->picoquic_connection) == picoquic_state_disconnected) {
    log_info("QUIC connection is disconnected, closing underlying handles");
    close_underlying_handles(CONNECTION_FROM_HANDLE(udp_handle));
    return;
  }

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
      log_trace("Send buffer size: %zu", send_buffer.len);
      // TODO - is it actually correct to modify the buffer directly like this?
      send_buffer.len = send_length;
      uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
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
      log_debug("Connection state is: %d", picoquic_get_cnx_state(quic_state->picoquic_connection));
    }
  } while (send_length > 0);
  log_debug("Finished sending QUIC packets");

  reset_quic_timer();
}

uv_udp_t* set_up_udp_handle(Connection* connection) {
  uv_udp_t* new_udp_handle = malloc(sizeof(uv_udp_t));
  if (new_udp_handle == NULL) {
    log_error("Failed to allocate memory for UDP handle");
    return NULL;
  }

  int rc = uv_udp_init(ctaps_event_loop, new_udp_handle);
  if (rc < 0) {
    log_error( "Error initializing udp handle: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }

  rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&connection->local_endpoint.data.address, 0);
  if (rc < 0) {
    log_error("Problem with auto-binding: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }

  rc = uv_udp_recv_start(new_udp_handle, alloc_quic_buf, on_quic_udp_read);
  if (rc < 0) {
    log_error("Error starting UDP receive: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }
  return new_udp_handle;
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
  // The current function snippet is just example code
  // We need to:
  //   - receive/send data over udp and feed it to picoquic
  //       - Should this be done over udp.c or just use the libuv udp logic directly here
  //       - Best to start with libuv logic, and then see what is actually needed and if we can move to udp.c
  //   - set up uv_timer_t for waking up picoquic when needed
  //
  //
  // If we are planning to use udp.c then it would make sense to have the udp handle in the connection
  // Therefore uv_timer_t should live in the quic context and udp connection in the connection?
  //
  // Maybe we can think about multistreaming already now
  //    - Are picoquic_quic_t unique per multistreamed connection?
  //    - How would it work for listening?
  //    - Seems like picoquic_cnx_t would be the individual, since that is where you enter client vs server mode
  //    - I assume we would only use a single "engine" having a single timer which supports all connections
  //    - Therefore uv_timer_t should live in some higher level context managing multiple connections?
  //    - Lets not think more about multistreaming right now
  //
  //    - udp handle -> connection, timer -> quic context

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  picoquic_cnx_t *cnx = NULL;
  int client_socket = -1;
  uint64_t current_time = 0;
  int ret = 0;

  /* 1. INITIALIZATION & CONTEXT SETUP */
  // Use a simplified initial context creation (replace with your TAPS-specific logic)

  current_time = picoquic_get_quic_time(quic_ctx);
  picoquic_connection_id_t local_cnx_id = {
    .id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
    .id_len = 8
  };
  picoquic_connection_id_t remote_cnx_id = {
    .id = {0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
    .id_len = 8
  };

  uv_udp_t* udp_handle = set_up_udp_handle(connection);

  uv_timer_t *timer_handle = set_up_timer_handle();

  if (default_global_quic_state.timer_handle == NULL) {
    default_global_quic_state.timer_handle = timer_handle;
  }
  else {
    log_error("QUIC global timer handle already set");
    return -EALREADY;
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

  // Creates the connection object and sends the first (Initial) packet.
  connection_state->picoquic_connection = picoquic_create_cnx(
      quic_ctx,
      local_cnx_id,
      remote_cnx_id,
      (struct sockaddr*) &connection->remote_endpoint.data.resolved_address,
      current_time,
      1,
      "localhost",
      "simple-ping",
      1
  );

  udp_handle->data = connection;
  timer_handle->data = connection;

  picoquic_set_callback(connection_state->picoquic_connection, picoquic_callback, connection);
  rc = picoquic_start_client_cnx(connection_state->picoquic_connection);
  if (rc != 0) {
    log_error("Error starting QUIC client connection: %d", rc);
    free(connection_state->udp_sock_name);
    free(connection_state);
    return rc;
  }

  reset_quic_timer();
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

  log_trace("Initiated closing of picoquic connection with return code %d", rc);

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
  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  if (default_global_quic_state.listener != NULL) {
    log_error("QUIC listener already set up for SocketManager %p", (void*)socket_manager);
    return -EALREADY;
  }
  socket_manager->active_connections = g_hash_table_new(g_bytes_hash, g_bytes_equal);

  default_global_quic_state.listener = socket_manager->listener;

  QuicConnectionState* listener_state = malloc(sizeof(QuicConnectionState));

  uv_udp_t* new_udp_handle = malloc(sizeof(uv_udp_t));
  if (new_udp_handle == NULL) {
    log_error("Failed to allocate memory for UDP handle");
    return -ENOMEM;
  }
  memset(new_udp_handle, 0, sizeof(uv_udp_t));

  // ############# INIT UDP HANDLE #############
  int rc = uv_udp_init(ctaps_event_loop, new_udp_handle);
  if (rc < 0) {
    log_error( "Error initializing udp handle: %s", uv_strerror(rc));
    free(new_udp_handle);
    return -ENOMEM;
  }

  LocalEndpoint local_endpoint = listener_get_local_endpoint(socket_manager->listener);

  rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&local_endpoint.data.address, 0);
  if (rc < 0) {
    log_error("Problem with auto-binding: %s", uv_strerror(rc));
    free(new_udp_handle);
    return -ENOMEM;
  }

  rc = uv_udp_recv_start(new_udp_handle, alloc_quic_buf, on_quic_udp_read);
  if (rc < 0) {
    log_error("Error starting UDP receive: %s", uv_strerror(rc));
    free(new_udp_handle);
    return -ENOMEM;
  }

  listener_state->udp_handle = new_udp_handle;

  socket_manager->protocol_state = listener_state;
  socket_manager->ref_count = 1;

  
  return 0;
}
int quic_stop_listen(SocketManager* socket_manager) {
  log_debug("Stopping QUIC listen");
  QuicConnectionState* quic_state = (QuicConnectionState*)socket_manager->protocol_state;
  int rc = uv_udp_recv_stop(quic_state->udp_handle);
  if (rc < 0) {
    log_error("Problem with stopping receive: %s\n", uv_strerror(rc));
    return rc;
  }
  return 0;
}
int quic_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer) {
  return -ENOSYS;
}
