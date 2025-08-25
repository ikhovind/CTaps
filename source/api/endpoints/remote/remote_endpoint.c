#include "remote_endpoint.h"

#include <ctaps.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

void remote_endpoint_build(RemoteEndpoint* remote_endpoint) {
  remote_endpoint->data.address.ss_family = AF_UNSPEC;
  memset(&remote_endpoint->data, 0, sizeof(remote_endpoint->data));
}

void remote_endpoint_with_ipv4(RemoteEndpoint* remote_endpoint, in_addr_t ipv4_addr) {
  remote_endpoint->type = ENDPOINT_TYPE_ADDRESS;

  struct sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  addr->sin_port = htons(remote_endpoint->port);
}

void remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint, struct in6_addr ipv6_addr) {
  remote_endpoint->type = ENDPOINT_TYPE_ADDRESS;

  struct sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.address;
  addr->sin6_family = AF_INET6;
  addr->sin6_addr = ipv6_addr;
  addr->sin6_port = htons(remote_endpoint->port);
}

int remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname) {
  remote_endpoint->type = ENDPOINT_TYPE_HOSTNAME;

  remote_endpoint->data.hostname = (char*) malloc(strlen(hostname) + 1);
  memcpy(remote_endpoint->data.hostname, hostname, strlen(hostname) + 1);
}

void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, const uint16_t port) {
  remote_endpoint->port = port;
  if (remote_endpoint->data.address.ss_family == AF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.address;
    addr->sin6_port = htons(port);
  }
  if (remote_endpoint->data.address.ss_family == AF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.address;
    addr->sin_port = htons(port);
  }
}