#include "udp.h"

#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>

#include "connections/connection/connection.h"
#include "ctaps.h"
#include "protocols/registry/protocol_registry.h"

static uv_udp_t send_socket;
static GQueue* udp_receive_queue;

void udp_recv_cb() {
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  // We'll use a static buffer for this simple example, but in a real
  // application, you would likely use malloc or a buffer pool.
  static char slab[65536];
  *buf = uv_buf_init(slab, sizeof(slab));
}

void on_send(uv_udp_send_t* req, int status) {
  if (status) {
    fprintf(stderr, "Send error: %s\n", uv_strerror(status));
  }
  if (req) {
    free(req);  // Free the send request
  }
}

void on_read(uv_udp_t* req, ssize_t nread, const uv_buf_t* buf,
             const struct sockaddr* addr, unsigned flags) {
  if (nread < 0) {
    fprintf(stderr, "Read error: %s\n", uv_err_name(nread));
    uv_close((uv_handle_t*)req, NULL);
    free(buf->base);
    return;
  }

  if (addr == NULL) {
    // No more data to read, or an empty packet.
    return;
  }

  Message* received_message = malloc(sizeof(Message));
  if (!received_message) {
    return;
  }

  received_message->content = malloc(nread);
  if (!received_message->content) {
    return;
  }

  // Print the received data (nread holds the number of bytes received)
  memcpy(received_message->content, buf->base, nread);

  g_queue_push_tail(udp_receive_queue, received_message);

  // Stop receiving so the event loop can exit
  uv_udp_recv_stop(req);
}

int udp_init(Connection* connection) {
  udp_receive_queue = g_queue_new();
  uv_udp_init(ctaps_event_loop, &send_socket);

  if (connection->local_endpoint.initialized) {
    // TODO - set local endpoint in test check that this works
    uv_udp_bind(
        &send_socket,
        (const struct sockaddr*)&connection->local_endpoint.addr.ipv4_addr, 0);
    printf("Listening for UDP packets on port %d...\n",
           ntohs(connection->local_endpoint.addr.ipv4_addr.sin_port));
  } else {
    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", 0, &bind_addr);

    uv_udp_bind(&send_socket, (const struct sockaddr*)&bind_addr, 0);
    printf("Listening for UDP packets on port %d...\n",
           ntohs(bind_addr.sin_port));
  }

  uv_udp_recv_start(&send_socket, alloc_buffer, on_read);
}

int udp_close() {
  g_queue_free(udp_receive_queue);
  return 0;
}

void register_udp_support() {
  register_protocol(&udp_protocol_interface);
}

int udp_send(Connection* connection, Message* message) {
  const uv_buf_t buffer =
      uv_buf_init(message->content, strlen(message->content));

  uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
  if (!send_req) {
    fprintf(stderr, "Failed to allocate send request\n");
    return 1;
  }

  uv_udp_send(
      send_req, &send_socket, &buffer, 1,
      (const struct sockaddr*)&connection->remote_endpoint.addr.ipv4_addr,
      on_send);

  return 0;
}

Message* udp_receive(Connection* connection) {
  if (g_queue_get_length(udp_receive_queue) > 0) {
    return g_queue_pop_tail(udp_receive_queue);
  }
  return 0;
}
