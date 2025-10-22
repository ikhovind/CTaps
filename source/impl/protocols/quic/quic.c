#include "quic.h"

#include <logging/log.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <picoquic.h>
#include "uv.h"


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
  //
  //

  picoquic_quic_t *quic_ctx = NULL;
  picoquic_cnx_t *cnx = NULL;
  int client_socket = -1;
  uint64_t current_time = 0;
  int ret = 0;

  /* 1. INITIALIZATION & CONTEXT SETUP */

  // Use a simplified initial context creation (replace with your TAPS-specific logic)
  quic_ctx = picoquic_create(
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

  current_time = picoquic_get_quic_time(quic_ctx);
  picoquic_connection_id_t local_cnx_id = {
    .id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
    .id_len = 8
  };
  picoquic_connection_id_t remote_cnx_id = {
    .id = {0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
    .id_len = 8
  };


  // Creates the connection object and sends the first (Initial) packet.
  cnx = picoquic_create_cnx(
      quic_ctx,               /* QUIC context */
      local_cnx_id, /* Initial connection ID (can be NULL) */
      remote_cnx_id, /* Destination address (manual setup required) */
      (struct sockaddr*) &connection->remote_endpoint.data.resolved_address,           /* Current time */
      current_time,                      /* QUIC version (0 is latest, IETF v1) */
      0,
      "127.0.0.1",
      "simple-ping",                       //* ALPN (Application-Layer Protocol Negotiation) */
      1
  );

  picoquic_get_next_wake_time(quic_ctx, picoquic_get_quic_time(quic_ctx));

  picoquic_state_enum cnx_state = picoquic_get_cnx_state(cnx);

  if (cnx_state == picoquic_state_ready) {
  }
  return -ENOSYS;
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
