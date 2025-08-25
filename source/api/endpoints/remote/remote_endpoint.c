#include "remote_endpoint.h"

#include <stdio.h>

void remote_endpoint_with_ipv4(RemoteEndpoint* remote_endpoint, in_addr_t ipv4_addr) {
  remote_endpoint->family = AF_INET;
  remote_endpoint->addr.ipv4_addr.sin_family = AF_INET;
  remote_endpoint->addr.ipv4_addr.sin_addr.s_addr = ipv4_addr;
}

void remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint, struct in6_addr ipv6_addr) {
  remote_endpoint->family = AF_INET6;
  remote_endpoint->addr.ipv6_addr.sin6_family = AF_INET6;
  remote_endpoint->addr.ipv6_addr.sin6_addr = ipv6_addr;
}

void remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname) {
  // TODO - implement
}

void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, unsigned short port) {
  remote_endpoint->addr.ipv4_addr.sin_port = htons(port);
  remote_endpoint->addr.ipv6_addr.sin6_port = htons(port);
}
