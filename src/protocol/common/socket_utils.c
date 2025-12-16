#include "socket_utils.h"

#include "ctaps.h"
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <uv.h>


uv_udp_t* create_udp_listening_on_local(ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
  bool is_ephemeral = (local_endpoint == NULL);
  if (!is_ephemeral) {
    log_debug("Creating UDP socket for set local endpoint");
    if (local_endpoint->data.address.ss_family == AF_INET) {
      log_trace("Creating UDP socket listening on IPv4 on port %d", ntohs(((struct sockaddr_in*)&local_endpoint->data.address)->sin_port));
    }
    else if (local_endpoint->data.address.ss_family == AF_INET6) {
      log_trace("Creating UDP socket listening on IPv6 on port %d", ntohs(((struct sockaddr_in6*)&local_endpoint->data.address)->sin6_port));
    }
    else {
      log_error("Local endpoint is not of type IPv4 or IPv6");
      return NULL;
    }
  }
  else {
    log_debug("Local endpoint is not set, creating UDP socket for ephemeral port");
  }
  uv_udp_t* new_udp_handle = malloc(sizeof(uv_udp_t));
  if (new_udp_handle == NULL) {
    log_error("Failed to allocate memory for UDP handle");
    return NULL;
  }

  int rc = uv_udp_init(event_loop, new_udp_handle);
  if (rc < 0) {
    log_error( "Error initializing udp handle: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }

  if (is_ephemeral) {
    struct sockaddr_in ephemeral_addr;
    uv_ip4_addr("0.0.0.0", 0, &ephemeral_addr);
    rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&ephemeral_addr, 0);
  }
  else {
    rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&local_endpoint->data.address, 0);
  }
  if (rc < 0) {
    log_error("Problem with auto-binding: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }

  // TODO - filter on remote endpoint
  rc = uv_udp_recv_start(new_udp_handle, alloc_cb, on_read_cb);
  if (rc < 0) {
    log_error("Error starting UDP receive: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }
  return new_udp_handle;
}

uv_udp_t* create_udp_listening_on_ephemeral(uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
  return create_udp_listening_on_local(NULL, alloc_cb, on_read_cb);
}
