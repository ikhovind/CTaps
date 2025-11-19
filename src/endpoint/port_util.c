#include "port_util.h"

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <logging/log.h>
#include <errno.h>

#include "ctaps.h"

int32_t get_service_port_inner(char* service, int family) {
  struct addrinfo hints;
  struct addrinfo *result;

  // Initialize hints struct
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // XMPP uses TCP

  const int status = getaddrinfo(NULL, service, &hints, &result);
  if (status != 0) {
    log_error("getaddrinfo error: %s\n", gai_strerror(status));
    return status;
  }

  int32_t res = -1;
  // --- Iterate through the results ---
  for (const struct addrinfo* rp = result; rp != NULL; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET && (family == AF_INET || family == AF_UNSPEC)) {
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
      res = ntohs(ipv4->sin_port);
    }
    if (rp->ai_family == AF_INET6 && family == AF_INET6) {
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
      res = ntohs(ipv6->sin6_port);
    }
  }

  freeaddrinfo(result);
  if (res == -1) {
    log_warn("Could not find port for service %s\n", service);
  }
  return res;
}

int32_t get_service_port_local(const ct_local_endpoint_t* local_endpoint) {
  return get_service_port_inner(local_endpoint->service, local_endpoint->data.address.ss_family);
}

int32_t get_service_port_remote(const ct_remote_endpoint_t* remote_endpoint) {
  return get_service_port_inner(remote_endpoint->service, remote_endpoint->data.resolved_address.ss_family);
}
