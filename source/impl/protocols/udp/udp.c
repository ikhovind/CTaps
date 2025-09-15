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
  printf("Initiating UDP connection\n");
  connection->received_messages = g_queue_new();
  connection->received_callbacks = g_queue_new();

  uv_udp_t* new_udp_handle;

  new_udp_handle = malloc(sizeof(*new_udp_handle));
  if (new_udp_handle == NULL) {
    perror("Failed to allocate memory for UDP handle");
    return -1; // Indicate memory allocation failure
  }

  int rc = uv_udp_init(ctaps_event_loop, new_udp_handle);
  if (rc < 0) {
    fprintf(stderr, "Error initializing udp handle: %s\n", uv_strerror(rc));
    free(new_udp_handle); // CRITICAL: Clean up allocated memory on failure
    return rc;
  }

  int num_found_addresses = 0;
  new_udp_handle->data = connection;
  struct sockaddr* found_interface_addrs = NULL;
  if (connection->local_endpoint.interface_name != NULL) {
    uv_interface_address_t* interfaces;
    int count;
    uv_interface_addresses(&interfaces, &count);

    found_interface_addrs = malloc(count);

    for (int i = 0; i < count; i++) {
      if (strcmp(interfaces[i].name, connection->local_endpoint.interface_name) == 0) {
        if (interfaces[i].address.address4.sin_family == AF_INET) {
          // check if address from interface is ipv4:
          if (interfaces[i].address.address4.sin_family == AF_INET) {
            found_interface_addrs[num_found_addresses++] = *(struct sockaddr*)&interfaces[i].address.address4;
          }
        }
      }
    }
    uv_free_interface_addresses(interfaces, count);
  }

  struct sockaddr_in bind_addr;
  if (num_found_addresses > 0) {
    printf("Using local endpoint from interface\n");

    bind_addr = *(struct sockaddr_in*)&found_interface_addrs[0];
  }
  else {
    printf("No address found from interface, binding to 0.0.0.0\n");
    uv_ip4_addr("0.0.0.0", 0, &bind_addr);
  }
  bind_addr.sin_port = htons(connection->local_endpoint.port);

  connection->local_endpoint.data.address = *(struct sockaddr_storage*)&bind_addr;

  rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&bind_addr, 0);
  if (rc < 0) {
    fprintf(stderr, "Problem with auto-binding: %s\n", uv_strerror(rc));
    free(new_udp_handle);
    free(found_interface_addrs);
    return rc;
  }

  rc = uv_udp_recv_start(new_udp_handle, alloc_buffer, on_read);
  if (rc < 0) {
    fprintf(stderr, "Problem with starting receive: %s\n", uv_strerror(rc));
    free(new_udp_handle);
    free(found_interface_addrs);
    return rc;
  }

  connection->protocol_uv_handle = (uv_handle_t*)new_udp_handle;

  init_done_cb.init_done_callback(connection, init_done_cb.user_data);

  free(found_interface_addrs);
  printf("Successfully initiated UDP connection\n");
  return 0;
}

void closed_handle_cb(uv_handle_t* handle) {
  printf("Successfully closed UDP handle\n");
}

int udp_close(const Connection* connection) {
  g_queue_free(connection->received_messages);
  g_queue_free(connection->received_callbacks);
  uv_udp_recv_stop((uv_udp_t*)connection->protocol_uv_handle);
  uv_close(connection->protocol_uv_handle, closed_handle_cb);
  return 0;
}

int udp_stop_listen(struct SocketManager* socket_manager) {
  printf("Trying to stop listening via UDP\n");
  uv_udp_recv_stop((uv_udp_t*)socket_manager->protocol_uv_handle);
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
      send_req, (uv_udp_t*)connection->protocol_uv_handle, &buffer, 1,
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

void socket_listen_callback(uv_udp_t* handle,
                               ssize_t nread,
                               const uv_buf_t* buf,
                               const struct sockaddr* addr,
                               unsigned flags) {
  printf("Socket manager read callback\n");
  if (nread == 0 && addr == NULL) {
    // No more data to read, or an empty packet.
    printf("Nothing to read from udp socket\n");
    return;
  }
  SocketManager *socket_manager = (SocketManager*)handle->data;

  Message* received_message = malloc(sizeof(Message));
  if (!received_message) {
    return;
  }
  received_message->content = malloc(nread);
  if (!received_message->content) {
    free(received_message);
    return;
  }
  received_message->length = nread;

  memcpy(received_message->content, buf->base, nread);

  socket_manager_multiplex_received_message(socket_manager, received_message, addr);
}

int udp_listen(SocketManager* socket_manager) {
  printf("Listening via UDP\n");
  uv_udp_t* udp_handle = malloc(sizeof(*udp_handle));
  if (udp_handle == NULL) {
    perror("Failed to allocate memory for UDP handle");
    return -1;
  }

  printf("Listening via UDP\n");
  socket_manager->active_connections = g_hash_table_new(g_bytes_hash, g_bytes_equal);
  socket_manager->ref_count = 1;
  Listener* listener = socket_manager->listener;

  int rc = uv_udp_init(ctaps_event_loop, udp_handle);
  if (rc < 0) {
    fprintf(stderr, "Error initializing udp handle: %s\n", uv_strerror(rc));
    free(udp_handle); // CRITICAL: Clean up memory on failure
    return rc;
  }

  struct sockaddr_in bind_addr;
  uv_ip4_addr("0.0.0.0", listener->local_endpoint.port, &bind_addr);
  printf("Local endpoint is initialized by user.\n");
  rc = uv_udp_bind(udp_handle, (const struct sockaddr*)&bind_addr, 0);
  if (rc < 0) {
    fprintf(stderr, "Problem with binding: %s\n", uv_strerror(rc));
    free(udp_handle);
    return rc;
  }

  udp_handle->data = socket_manager;

  rc = uv_udp_recv_start(udp_handle, alloc_buffer, socket_listen_callback);
  if (rc < 0) {
    fprintf(stderr, "Problem with starting receive: %s\n", uv_strerror(rc));
    free(udp_handle);
    return rc;
  }

  socket_manager->protocol_uv_handle = (uv_handle_t*)udp_handle;

  return 0;
}
