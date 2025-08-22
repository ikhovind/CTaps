#include "udp.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>

#include "connections/connection/connection.h"
#include "ctaps.h"
#include "protocols/registry/protocol_registry.h"

typedef int (*ReceiveMessageCb)(struct Connection* connection,
                                Message** received_message);

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
  Connection* connection = (Connection*)req->data;
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

  if (g_queue_is_empty(connection->received_callbacks)) {
    g_queue_push_tail(connection->received_messages, received_message);
    // It works when I invoke it here???:
    // uv_udp_recv_stop(&connection->udp_handle);
  }

  else {
    const ReceiveMessageCb receive_message_cb =
        g_queue_pop_head(connection->received_callbacks);

    receive_message_cb(connection, &received_message);
  }
}

int udp_init(Connection* connection,
             int (*init_done_cb)(Connection* connection)) {
  connection->received_messages = g_queue_new();
  connection->received_callbacks = g_queue_new();
  uv_udp_init(ctaps_event_loop, &connection->udp_handle);

  connection->udp_handle.data = connection;

  if (connection->local_endpoint.initialized) {
    uv_udp_bind(
        &connection->udp_handle,
        (const struct sockaddr*)&connection->local_endpoint.addr.ipv4_addr, 0);
    printf("Listening for UDP packets on port %d...\n",
           ntohs(connection->local_endpoint.addr.ipv4_addr.sin_port));
  } else {
    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", 0, &bind_addr);

    uv_udp_bind(&connection->udp_handle, (const struct sockaddr*)&bind_addr, 0);
    printf("Listening for UDP packets on port %d...\n",
           ntohs(bind_addr.sin_port));
  }

  uv_udp_recv_start(&connection->udp_handle, alloc_buffer, on_read);
  init_done_cb(connection);
}

void test(struct uv_handle_s* a) {
}

int udp_close(const Connection* connection) {
  g_queue_free(connection->received_messages);
  g_queue_free(connection->received_callbacks);
  uv_udp_recv_stop(&connection->udp_handle);
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
      send_req, &connection->udp_handle, &buffer, 1,
      (const struct sockaddr*)&connection->remote_endpoint.addr.ipv4_addr,
      on_send);

  return 0;
}

int udp_receive(Connection* connection, ReceiveMessageCb receive_msg_cb) {
  // If we have a message to receive then simply return that
  if (!g_queue_is_empty(connection->received_messages)) {
    Message* received_message = g_queue_pop_head(connection->received_messages);
    receive_msg_cb(connection, &received_message);
    return 0;
  }
  // If we don't have a message to receive, add the callback to the queue of
  // waiting callbacks
  g_queue_push_tail(connection->received_callbacks, receive_msg_cb);
  return 0;
}