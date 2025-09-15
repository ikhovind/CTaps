#include "local_endpoint.h"

#include <stdio.h>
#include <string.h>

void local_endpoint_with_port(LocalEndpoint* local_endpoint, int port) {
  local_endpoint->port = port;
  if (local_endpoint->data.address.ss_family == AF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint->data.address;
    addr->sin6_port = htons(port);
  }
  if (local_endpoint->data.address.ss_family == AF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.address;
    addr->sin_port = htons(port);
  }
}

void local_endpoint_build(LocalEndpoint* local_endpoint) {
  memset(local_endpoint, 0, sizeof(LocalEndpoint));
}

void local_endpoint_with_ipv4(LocalEndpoint* local_endpoint, in_addr_t ipv4_addr) {
  local_endpoint->type = LOCAL_ENDPOINT_TYPE_ADDRESS;
  struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  addr->sin_port = htons(local_endpoint->port);
}

void local_endpoint_with_ipv6(LocalEndpoint* local_endpoint, struct in6_addr ipv6_addr) {
  local_endpoint->type = LOCAL_ENDPOINT_TYPE_ADDRESS;
  struct sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint->data.address;
  addr->sin6_family = AF_INET6;
  addr->sin6_addr = ipv6_addr;
  addr->sin6_port = htons(local_endpoint->port);
}

void local_endpoint_with_interface(LocalEndpoint* local_endpoint, char* interface_name) {
  local_endpoint->interface_name = interface_name;
}
