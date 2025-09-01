#include "udp.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>
#include <connections/listener/listener.h>

#include "connections/connection/connection.h"
#include "ctaps.h"
#include "connections/listener/socket_manager/socket_manager.h"
#include "protocols/registry/protocol_registry.h"

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

void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
             const struct sockaddr* addr, unsigned flags) {
  Connection* connection = (Connection*)handle->data;
  if (nread < 0) {
    fprintf(stderr, "Read error: %s\n", uv_err_name(nread));
    uv_close((uv_handle_t*)handle, NULL);
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
  received_message->length = nread;

  memcpy(received_message->content, buf->base, nread);

  if (g_queue_is_empty(connection->received_callbacks)) {
    g_queue_push_tail(connection->received_messages, received_message);
  }

  else {
    printf("We have a receive callback ready\n");
    ReceiveMessageRequest* receive_callback =
        g_queue_pop_head(connection->received_callbacks);

    receive_callback->receive_cb(connection, &received_message, receive_callback->user_data);
    free(receive_callback);
  }
}

int udp_init(Connection* connection, InitDoneCb init_done_cb) {
  connection->received_messages = g_queue_new();
  connection->received_callbacks = g_queue_new();
  int udp_handle_rc = uv_udp_init(ctaps_event_loop, &connection->udp_handle);
  if (udp_handle_rc < 0) {
    printf("Error with udp handle: %d\n", udp_handle_rc);
    return udp_handle_rc;
  }

  connection->udp_handle.data = connection;

  if (connection->local_endpoint.type == LOCAL_ENDPOINT_TYPE_ADDRESS) {
    printf("Local endpoint is initialized by user.\n");
    int bind_rc = uv_udp_bind(&connection->udp_handle, (const struct sockaddr*)&connection->local_endpoint.data.address, 0);
    if (bind_rc < 0) {
      return bind_rc;
    }
  } else {
    printf("Local endpoint is not initialized by user.\n");
    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", 0, &bind_addr);

    int bind_rc = uv_udp_bind(&connection->udp_handle, (const struct sockaddr*)&bind_addr, 0);
    if (bind_rc < 0) {
      return bind_rc;
    }
    printf("Listening for UDP packets on port %d...\n",
           ntohs(bind_addr.sin_port));
  }

  int recv_start_rc = uv_udp_recv_start(&connection->udp_handle, alloc_buffer, on_read);
  if (recv_start_rc < 0) {
    return recv_start_rc;
  }
  init_done_cb.init_done_callback(connection, init_done_cb.user_data);
}

int udp_close(const Connection* connection) {
  g_queue_free(connection->received_messages);
  g_queue_free(connection->received_callbacks);
  uv_udp_recv_stop(&connection->udp_handle);
  return 0;
}

int udp_stop_listen(struct Listener* listener) {
  // TODO - free connections which are contained in the socket manager
  uv_udp_recv_stop(&listener->socket_manager->udp_handle);
  return 0;
}

void register_udp_support() {
  register_protocol(&udp_protocol_interface);
}

int udp_send(Connection* connection, Message* message) {
  printf("Sending message: %s\n", message->content);
  const uv_buf_t buffer =
      uv_buf_init(message->content, message->length);

  uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
  if (!send_req) {
    fprintf(stderr, "Failed to allocate send request\n");
    return 1;
  }

  return uv_udp_send(
      send_req, &connection->udp_handle, &buffer, 1,
      (const struct sockaddr*)&connection->remote_endpoint.data.address,
      on_send);
}

int udp_receive(Connection* connection, ReceiveMessageRequest receive_msg_cb) {
  // If we have a message to receive then simply return that
  printf("UDP receiving\n");
  if (!g_queue_is_empty(connection->received_messages)) {
    Message* received_message = g_queue_pop_head(connection->received_messages);
    receive_msg_cb.receive_cb(connection, &received_message, receive_msg_cb.user_data);
    return 0;
  }
  printf("Adding received callback to callback queue\n");

  ReceiveMessageRequest* ptr = malloc(sizeof(receive_msg_cb));
  memcpy(ptr, &receive_msg_cb, sizeof(receive_msg_cb));

  // If we don't have a message to receive, add the callback to the queue of
  // waiting callbacks
  g_queue_push_tail(connection->received_callbacks, ptr);
  return 0;
}


void connection_received(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
             const struct sockaddr* addr, unsigned flags) {
}

int udp_listen(struct Listener* listener) {
  SocketManager* socket_manager;
  return socket_manager_create(socket_manager, listener);
}
