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
  remote_endpoint->type = REMOTE_ENDPOINT_TYPE_ADDRESS;

  struct sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  addr->sin_port = htons(remote_endpoint->port);
}

void remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint, struct in6_addr ipv6_addr) {
  remote_endpoint->type = REMOTE_ENDPOINT_TYPE_ADDRESS;

  struct sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.address;
  addr->sin6_family = AF_INET6;
  addr->sin6_addr = ipv6_addr;
  addr->sin6_port = htons(remote_endpoint->port);
}

void remote_endpoint_from_sockaddr(RemoteEndpoint* remote_endpoint, const struct sockaddr* addr) {
  memset(remote_endpoint, 0, sizeof(RemoteEndpoint));
  if (addr->sa_family == AF_INET) {
    struct sockaddr_in* in_addr = (struct sockaddr_in*)addr;
    remote_endpoint->type = REMOTE_ENDPOINT_TYPE_ADDRESS;
    remote_endpoint->port = ntohs(in_addr->sin_port);
    remote_endpoint->data.address = *((struct sockaddr_storage*)addr);
  }
  else if (addr->sa_family == AF_INET6) {
    struct sockaddr_in6* in6_addr = (struct sockaddr_in6*)addr;
    remote_endpoint->type = REMOTE_ENDPOINT_TYPE_ADDRESS;
    remote_endpoint->port = ntohs(in6_addr->sin6_port);
    remote_endpoint->data.address = *((struct sockaddr_storage*)addr);
  }
  else {
    printf("Unsupported address family: %d\n", addr->sa_family);
  }
}

int remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname) {
  remote_endpoint->type = REMOTE_ENDPOINT_TYPE_HOSTNAME;

  printf("About to malloc\n");
  remote_endpoint->data.hostname = (char*) malloc(strlen(hostname) + 1);
  printf("after malloc\n");
  if (remote_endpoint->data.hostname == NULL) {
    return -1;
  }
  printf("About to memcpy\n");
  memcpy(remote_endpoint->data.hostname, hostname, strlen(hostname) + 1);
  return 0;
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