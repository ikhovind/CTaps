#include "quic.h"

#include <ctaps.h>
#include <logging/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <picoquic.h>
#include "connections/connection/connection.h"
#include "uv.h"

#define MAX_QUIC_PACKET_SIZE 1500
static picoquic_quic_t* global_quic_ctx;

typedef struct QuicConnectionContext {
  Connection* connection;
  uv_udp_t* udp_handle;
  uv_timer_t* timer_handle;
  struct sockaddr_storage* udp_sock_name;
} QuicConnectionContext;

picoquic_quic_t* get_global_quic_ctx() {
  if (global_quic_ctx == NULL) {
    global_quic_ctx = picoquic_create(
       1,
       NULL,
       NULL,
       NULL,
       "simple-ping",
       NULL,
       NULL,
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

void notify_if_cnx_ready(Connection* connection, picoquic_cnx_t* cnx) {
  if (cnx == NULL || connection == NULL || connection->connection_callbacks.ready == NULL) {
    return;
  }
  picoquic_state_enum state = picoquic_get_cnx_state(cnx);
  if (state == picoquic_state_ready) {
    connection->connection_callbacks.ready(connection, connection->connection_callbacks.user_data);
  }
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
    // No more data to read, or an empty packet.
    return;
  }

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  picoquic_cnx_t *cnx = NULL;

  QuicConnectionContext* quic_ctx_data = (QuicConnectionContext*)udp_handle->data;
  struct sockaddr_storage* addr_to = quic_ctx_data->udp_sock_name;

  int rc = picoquic_incoming_packet_ex(
    quic_ctx,
    buf->base,
    buf->len,
    addr_from,
    addr_to,
    0,
    0,
    &cnx,
    picoquic_get_quic_time(quic_ctx)
  );
  if (rc != 0) {
    log_error("Error processing incoming QUIC packet: %d", rc);
    // TODO - error handling
  }

  notify_if_cnx_ready(quic_ctx_data->connection, cnx);
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
  unsigned char send_buffer_base[MAX_QUIC_PACKET_SIZE];
  QuicConnectionContext* quic_ctx_data = (QuicConnectionContext*)timer_handle->data;
  uv_udp_t* udp_handle = quic_ctx_data->udp_handle;

  picoquic_quic_t* quic_ctx = get_global_quic_ctx();
  size_t send_length = 0;

  uv_buf_t send_buffer = uv_buf_init(send_buffer_base, MAX_QUIC_PACKET_SIZE);

  struct sockaddr_storage from_address;
  struct sockaddr_storage to_address;
  int if_index = -1;
  picoquic_cnx_t* last_cnx = NULL;
  size_t send_msg_size = 0;

  do {
    int rc = picoquic_prepare_next_packet_ex(
      quic_ctx,
      picoquic_get_quic_time(quic_ctx),
      send_buffer_base,
      MAX_QUIC_PACKET_SIZE,
      &send_length,
      &to_address,
      &from_address,
      &if_index,
      NULL,
      &last_cnx,
      &send_msg_size
    );
    if (rc != 0) {
      log_error("Error preparing next QUIC packet: %d", rc);
      // TODO - error handling
      break;
    }
    uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
    rc = uv_udp_send(
      send_req,
      udp_handle,
      &send_buffer,
      1,
      &to_address,
      on_quic_udp_send);
    if (rc < 0) {
      log_error("Error sending QUIC packet over UDP: %s", uv_strerror(rc));
      free(send_req);
      // TODO - error handling
      break;
    }
    notify_if_cnx_ready(quic_ctx_data->connection, last_cnx);
  } while (send_length > 0);

  uint64_t next_wake_delay = picoquic_get_next_wake_delay(quic_ctx, picoquic_get_quic_time(quic_ctx), INT64_MAX - 1);
  uv_timer_start(timer_handle, on_quic_timer, next_wake_delay, 0);
  // step 3: Report issues through the {{error-notify-API}}
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

  timer_handle->data = udp_handle;

  QuicConnectionContext* connection_context = malloc(sizeof(QuicConnectionContext));

  *connection_context = (QuicConnectionContext){
    .connection = connection,
    .timer_handle = timer_handle,
    .udp_handle = udp_handle,
    .udp_sock_name = malloc(sizeof(struct sockaddr_storage)),
  };

  int namelen = sizeof(struct sockaddr_storage);
  int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)connection_context->udp_sock_name, &namelen);
  if (rc < 0) {
    log_error("Error getting UDP socket name: %s", uv_strerror(rc));
    log_error("Error code: %d", rc);
    free(connection_context->udp_sock_name);
    free(connection_context);
    return rc;
  }

  udp_handle->data = connection_context;
  timer_handle->data = connection_context;

  // Creates the connection object and sends the first (Initial) packet.
  cnx = picoquic_create_cnx(
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

  uint64_t next_wake_delay = picoquic_get_next_wake_delay(quic_ctx, picoquic_get_quic_time(quic_ctx), INT64_MAX - 1);

  uv_timer_start(timer_handle, on_quic_timer, next_wake_delay, 0);
  return 0;
}

int quic_close(const Connection* connection) {
  return -ENOSYS;
}
int quic_send(Connection* connection, Message* message, MessageContext*) {
  return -ENOSYS;
}
int quic_receive(Connection* connection, ReceiveCallbacks receive_callbacks) {
  return -ENOSYS;
}
int quic_listen(struct SocketManager* socket_manager) {
  return -ENOSYS;
}
int quic_stop_listen(struct SocketManager* listener) {
  return -ENOSYS;
}
int quic_remote_endpoint_from_peer(uv_handle_t* peer, RemoteEndpoint* resolved_peer) {
  return -ENOSYS;
}
