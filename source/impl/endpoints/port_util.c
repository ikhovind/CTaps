#include "port_util.h"

#include <endpoints/remote/remote_endpoint.h>
#include <stdlib.h>
#include <string.h>

int32_t get_service_port_inner(char* service, int family) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int status;
  char ip_str[INET6_ADDRSTRLEN];

  // Initialize hints struct
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // XMPP uses TCP

  // --- The Lookup Invocation ---
  // Resolve the service "xmpp-client" for the host "jabber.org"
  status = getaddrinfo(NULL, service, &hints, &result);
  if (status != 0) {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    return 1;
  }

  // --- Iterate through the results ---
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    void *addr;
    uint16_t port;
    // get family of local_endpoint

    if (rp->ai_family == AF_INET && (family == AF_INET || family == AF_UNSPEC)) {
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
      return ntohs(ipv4->sin_port);
    }
    if (rp->ai_family == AF_INET6 && family == AF_INET6) {
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
      return ntohs(ipv6->sin6_port);
    }
    return -1;
  }

  // Free the linked list
  freeaddrinfo(result);
  return -1;
}

int32_t get_service_port_local(LocalEndpoint* local_endpoint) {
  return get_service_port_inner(local_endpoint->service, local_endpoint->data.address.ss_family);
}

int32_t get_service_port_remote(RemoteEndpoint* remote_endpoint) {
  return get_service_port_inner(remote_endpoint->service, remote_endpoint->data.resolved_address.ss_family);
}
