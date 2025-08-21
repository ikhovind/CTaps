#include "remote_endpoint.h"

#include <stdio.h>
#include <string.h>

void remote_endpoint_with_family(RemoteEndpoint* remote_endpoint,
                                 sa_family_t family) {
  memset(&remote_endpoint->addr.ipv4_addr, 0,
         sizeof(remote_endpoint->addr.ipv4_addr));
  remote_endpoint->family = family;
  remote_endpoint->addr.ipv4_addr.sin_family = AF_INET;
}

// TODO - this does not currently support actual hostnames, only ip addresses
void remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint,
                                   const char* hostname) {
  if (remote_endpoint->family == AF_INET) {
    remote_endpoint->addr.ipv4_addr.sin_addr.s_addr = inet_addr(hostname);
  } else if (remote_endpoint->family == AF_INET6) {
  } else {
    printf("remote_endpoint_with_port error\n");
  }
}

void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, int port) {
  if (remote_endpoint->family == AF_INET) {
    remote_endpoint->addr.ipv4_addr.sin_port = htons(port);
  } else if (remote_endpoint->family == AF_INET6) {
  } else {
    printf("remote_endpoint_with_port error\n");
  }
}
