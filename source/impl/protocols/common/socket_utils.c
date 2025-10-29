#include "socket_utils.h"

#include <connections/connection/connection.h>
#include <connections/listener/listener.h>
#include <glib.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "ctaps.h"


uv_udp_t* create_udp_listening_on_local(LocalEndpoint* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
  if (local_endpoint == NULL) {
    log_error("Local endpoint is NULL");
    return NULL;
  }
  if (local_endpoint->data.address.ss_family != AF_INET && local_endpoint->data.address.ss_family != AF_INET6) {
    log_error("Local endpoint is not of type IPv4 or IPv6");
    return NULL;
  }
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

  rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&local_endpoint->data.address, 0);
  if (rc < 0) {
    log_error("Problem with auto-binding: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }

  rc = uv_udp_recv_start(new_udp_handle, alloc_cb, on_read_cb);
  if (rc < 0) {
    log_error("Error starting UDP receive: %s", uv_strerror(rc));
    free(new_udp_handle);
    return NULL;
  }
  return new_udp_handle;
}
