#include "util.h"

#include <string.h>
#include <uv.h>

void get_interface_addresses(LocalEndpoint *local_endpoint, int *num_found_addresses, struct sockaddr_storage *output_interface_addrs) {
  *num_found_addresses = 0;
  if (local_endpoint->interface_name != NULL) {
    uv_interface_address_t* interfaces;
    int count;
    int rc = uv_interface_addresses(&interfaces, &count);
    if (rc != 0) {
      return;
    }

    for (int i = 0; i < count; i++) {
      if (strcmp(interfaces[i].name, local_endpoint->interface_name) == 0) {
        if (interfaces[i].address.address4.sin_family == AF_INET) {
          memcpy(&output_interface_addrs[(*num_found_addresses)++], &interfaces[i].address, sizeof(struct sockaddr_in));
        }
        if (interfaces[i].address.address4.sin_family == AF_INET6) {
          memcpy(&output_interface_addrs[(*num_found_addresses)++], &interfaces[i].address, sizeof(struct sockaddr_in6));
        }
        if (*num_found_addresses >= MAX_FOUND_INTERFACE_ADDRS) {
          break;
        }
      }
    }
    uv_free_interface_addresses(interfaces, count);
  }
}


int32_t get_service_port(LocalEndpoint* local_endpoint) {
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
  status = getaddrinfo(NULL, local_endpoint->service, &hints, &result);
  if (status != 0) {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    return 1;
  }

  printf("Results for xmpp-client service at jabber.org:\n\n");

  // --- Iterate through the results ---
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    void *addr;
    uint16_t port;
    // get family of local_endpoint

    if (rp->ai_family == AF_INET && local_endpoint->data.address.ss_family == AF_INET) { // IPv4
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
      return ntohs(ipv4->sin_port);
    }
    if (rp->ai_family == AF_INET6 && local_endpoint->data.address.ss_family == AF_INET6) { // IPv6
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
      return ntohs(ipv6->sin6_port);
    }
    return -1;
  }

  // Free the linked list
  freeaddrinfo(result);
  return 0;
}
