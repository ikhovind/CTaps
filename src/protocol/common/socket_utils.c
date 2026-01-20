#include "socket_utils.h"
#include "endpoint/local_endpoint.h"
#include "connection/connection.h"

#include "ctaps.h"
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <uv.h>


uv_udp_t* create_udp_listening_on_local(ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
  bool is_ephemeral = local_endpoint_get_resolved_port(local_endpoint) == 0;
  if (!is_ephemeral) {
    log_debug("Creating UDP socket for set local endpoint");
    if (local_endpoint_get_address_family(local_endpoint) == AF_INET) {
      log_trace("Creating UDP socket listening on IPv4 on port %d", ntohs(local_endpoint_get_resolved_port(local_endpoint)));
    }
    else if (local_endpoint_get_address_family(local_endpoint) == AF_INET6) {
      log_trace("Creating UDP socket listening on IPv6 on port %d", ntohs(local_endpoint_get_resolved_port(local_endpoint)));
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
    log_debug("Binding UDP socket to ephemeral port");
    struct sockaddr_in ephemeral_addr;
    uv_ip4_addr("0.0.0.0", 0, &ephemeral_addr);
    rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&ephemeral_addr, 0);
  }
  else {
    log_debug("Binding UDP socket to specified local endpoint");
    rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)local_endpoint_get_resolved_address(local_endpoint), 0);
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


int resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection) {
  switch (handle->type) {
    case UV_UDP: {
      uv_udp_t* udp_handle = (uv_udp_t*)handle;
      struct sockaddr_storage addr;
      int namelen = sizeof(addr);
      int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)&addr, &namelen);
      if (rc < 0) {
        log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
        return rc;
      }
      connection_set_resolved_local_address(connection, &addr);
      return 0;
    }
    case UV_TCP: {
      uv_tcp_t* tcp_handle = (uv_tcp_t*)handle;
      struct sockaddr_storage addr;
      int namelen = sizeof(addr);
      int rc = uv_tcp_getsockname(tcp_handle, (struct sockaddr*)&addr, &namelen);
      if (rc < 0) {
        log_error("Failed to get TCP socket name: %s", uv_strerror(rc));
        return rc;
      }
      connection_set_resolved_local_address(connection, &addr);
      return 0;
    }
    default:
      log_error("Unsupported handle type for resolving local endpoint: %d", handle->type);
      return -EINVAL;
  }
}
