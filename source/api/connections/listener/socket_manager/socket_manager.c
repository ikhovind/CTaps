#include "socket_manager.h"

#include <ctaps.h>
#include <connections/connection/connection.h>
#include <connections/listener/listener.h>
#include <glib/gbytes.h>

void socket_manager_alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  // We'll use a static buffer for this simple example, but in a real
  // application, you would likely use malloc or a buffer pool.
  static char slab[65536];
  *buf = uv_buf_init(slab, sizeof(slab));
}

void on_socket_manager_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
  printf("Socket manager read callback, nread is: %lu\n", nread);
  if (nread < 0) {
    fprintf(stderr, "Read error: %s\n", uv_err_name(nread));
    uv_close((uv_handle_t*)handle, NULL);
    free(buf->base);
    return;
  }
  if (addr == NULL) {
    printf("Error with addr value");
    // No more data to read, or an empty packet.
    return;
  }


  Listener* listener = (SocketManager*)handle->data;

  // get source address
  GBytes* addr_bytes = g_bytes_new(addr, sizeof(struct sockaddr_in));
  Connection* connection = g_hash_table_lookup(listener->socket_manager->active_connections, addr_bytes);
  // print the value of addr_bytes

  const void* addr_data = g_bytes_get_data(addr_bytes, NULL);

  // print the address in human readable form
  char addr_str[INET_ADDRSTRLEN];
  struct sockaddr_in* addr_in = (struct sockaddr_in*)addr;
  uv_ip4_name(addr_in, addr_str, sizeof(addr_str));

  Message* received_message = malloc(sizeof(Message));
  if (!received_message) {
    printf("Could not allocate memory for received message\n");
    return;
  }
  received_message->content = malloc(nread);
  if (!received_message->content) {
    free(received_message);
    return;
  }
  received_message->length = nread;

  memcpy(received_message->content, buf->base, nread);

  if (connection == NULL) {
    printf("No connection found, creating new one\n");
    Connection* connection = malloc(sizeof(Connection));
    connection_build_from_listener(connection, listener, (RemoteEndpoint*)&addr_bytes);
    // insert connection into hash table
    g_hash_table_insert(listener->socket_manager->active_connections, addr_bytes, connection);
    g_queue_push_tail(connection->received_messages, received_message);
    listener->connection_received_cb(listener, connection);
  }
  else {
    printf("Connection found, using existing one\n");
    if (g_queue_is_empty(connection->received_callbacks)) {
      g_queue_push_tail(connection->received_messages, received_message);
    }
    else {
      printf("We have a receive callback ready\n");
      ReceiveMessageRequest* receive_callback = g_queue_pop_head(connection->received_callbacks);

      receive_callback->receive_cb(connection, &received_message, receive_callback->user_data);
      free(receive_callback);
    }
  }
}

int socket_manager_create(SocketManager* socket_manager, Listener* listener) {
  memset(socket_manager, 0, sizeof(SocketManager));
  socket_manager->active_connections = g_hash_table_new(g_bytes_hash, g_bytes_equal);
  socket_manager->on_read = on_socket_manager_read;
  socket_manager->ref_count = 1;
  int udp_handle_rc = uv_udp_init(ctaps_event_loop, &socket_manager->udp_handle);
  if (udp_handle_rc < 0) {
    printf("Error with udp handle: %d\n", udp_handle_rc);
    return udp_handle_rc;
  }

  socket_manager->udp_handle.data = listener;

  if (listener->local_endpoint.type == LOCAL_ENDPOINT_TYPE_ADDRESS) {
    printf("Local endpoint is initialized by user.\n");
    int bind_rc = uv_udp_bind(&socket_manager->udp_handle, (const struct sockaddr*)&listener->local_endpoint.data.address, 0);
    if (bind_rc < 0) {
      printf("Problem with binding\n");
      return bind_rc;
    }
  } else {
    // local endpoint must be intialized for listener
    printf("Local endpoint not initialized for listener\n");
    return -1;
  }

  int recv_start_rc = uv_udp_recv_start(&socket_manager->udp_handle, socket_manager_alloc_buffer, on_socket_manager_read);
  if (recv_start_rc < 0) {
    printf("problem with receive start");
    return recv_start_rc;
  }
  return 0;
}
