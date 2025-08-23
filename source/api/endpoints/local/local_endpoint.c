#include "local_endpoint.h"

#include <stdio.h>
#include <string.h>

void local_endpoint_with_port(LocalEndpoint* local_endpoint, int port) {
  local_endpoint->addr.ipv4_addr.sin_port = htons(port);
  local_endpoint->initialized = true;
}

void local_endpoint_with_family(LocalEndpoint* local_endpoint, sa_family_t family) {
  memset(&local_endpoint->addr.ipv4_addr, 0,
         sizeof(local_endpoint->addr.ipv4_addr));
  local_endpoint->family = family;
  local_endpoint->addr.ipv4_addr.sin_family = AF_INET;
}

void local_endpoint_with_hostname(LocalEndpoint* local_endpoint, const char* hostname) {
  if (local_endpoint->family == AF_INET) {
    local_endpoint->addr.ipv4_addr.sin_addr.s_addr = inet_addr(hostname);
  } else if (local_endpoint->family == AF_INET6) {
  } else {
    printf("remote_endpoint_with_port error\n");
  }
}
