#include "socket_utils.h"
#include "endpoint/local_endpoint.h"
#include "connection/connection.h"

#include "ctaps.h"
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <uv.h>


uv_udp_t* create_udp_listening_on_local(const ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
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
  if (!new_udp_handle) {
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
    struct sockaddr_in ephemeral_addr = {0};
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

int get_sockaddr_from_handle(const uv_handle_t* handle, struct sockaddr_storage* addr) {
  int namelen = sizeof(struct sockaddr_storage);
  switch (handle->type) {
    case UV_UDP: {
      log_trace("Resolving local endpoint from UDP handle");
      uv_udp_t* udp_handle = (uv_udp_t*)handle;
      int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)addr, &namelen);
      if (rc < 0) {
        log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
        return rc;
      }
      break;
    }
    case UV_TCP: {
      log_trace("Resolving local endpoint from TCP handle");
      uv_tcp_t* tcp_handle = (uv_tcp_t*)handle;
      int rc = uv_tcp_getsockname(tcp_handle, (struct sockaddr*)addr, &namelen);
      if (rc < 0) {
        log_error("Failed to get TCP socket name: %s", uv_strerror(rc));
        return rc;
      }
      break;
    }
    default:
      log_error("Unsupported handle type for resolving local endpoint: %d", handle->type);
      return -EINVAL;
  }
  return 0;
}


int resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection) {
  if (!handle || !connection) {
    log_error("Handle or connection is NULL in resolve_local_endpoint_from_handle");
    log_debug("Handle pointer: %p, connection pointer: %p", (void*)handle, (void*)connection);
    return -EINVAL;
  }
  ct_local_endpoint_t local_endpoint = {0};
  struct sockaddr_storage addr = {0};;
  int rc = get_sockaddr_from_handle(handle, &addr);
  if (rc != 0) {
    log_error("Failed to get socket address from handle: %d", rc);
    return rc;
  }

  rc = ct_local_endpoint_from_sockaddr(&local_endpoint, &addr);
  if (rc != 0) {
    log_error("Failed to create local endpoint from sockaddr: %d", rc);
    return rc;
  }

  char local_ip[INET6_ADDRSTRLEN];
  uint16_t local_port;
  ct_get_addr_string(&addr, local_ip, sizeof(local_ip), &local_port);
  log_info("Resolved local endpoint from handle: %s:%d", local_ip, local_port);

  if (!ct_address_is_unspecified(&addr)) {
    log_debug("Setting active local endpoint on connection to resolved local endpoint");
    rc = ct_connection_set_active_local_endpoint(connection, &local_endpoint);
    if (rc != 0) {
      log_error("Failed to set active local endpoint on connection: %d", rc);
      return rc;
    }
  }
  else {
    log_debug("Local address is wildcard, not setting active local endpoint on connection");
  }

  log_debug("Setting all local port on connection to %d", ntohs(local_endpoint_get_resolved_port(&local_endpoint)));

  rc = ct_connection_set_all_local_port(connection, local_endpoint_get_resolved_port(&local_endpoint));
  if (rc != 0) {
    log_error("Failed to set local port on connection: %d", rc);
    return rc;
  }
  return 0;
}

bool ct_address_is_unspecified(const struct sockaddr_storage* addr) {
  log_trace("Checking if address is wildcard");
  if (addr->ss_family == AF_INET) {
    struct sockaddr_in* in_addr = (struct sockaddr_in*)addr;
    return in_addr->sin_addr.s_addr == INADDR_ANY;
  }
  else if (addr->ss_family == AF_INET6) {
    struct sockaddr_in6* in6_addr = (struct sockaddr_in6*)addr;
    return IN6_IS_ADDR_UNSPECIFIED(&in6_addr->sin6_addr);
  }
  return addr->ss_family == AF_UNSPEC;
}

void ct_get_addr_string(const struct sockaddr_storage* addr, char* buffer, size_t buffer_len, uint16_t* port) {
  if (buffer_len != INET6_ADDRSTRLEN) {
    log_error("Buffer length for address string is incorrect, expected %d but got %zu", INET6_ADDRSTRLEN, buffer_len);
    return;
  }
  if (addr->ss_family == AF_INET) {
    log_info("Getting string representation of IPv4 address");
    struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)addr;
    inet_ntop(AF_INET, &ipv4_addr->sin_addr, buffer, INET6_ADDRSTRLEN);
    *port = ntohs(ipv4_addr->sin_port);
  } else if (addr->ss_family == AF_INET6) {
    log_info("Getting string representation of IPv6 address");
    struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)addr;
    inet_ntop(AF_INET6, &ipv6_addr->sin6_addr, buffer, INET6_ADDRSTRLEN);
    *port = ntohs(ipv6_addr->sin6_port);
  } else {
    snprintf(buffer, INET6_ADDRSTRLEN, "<unknown family %d>", addr->ss_family);
    *port = 0;
  }
}
