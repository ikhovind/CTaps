#ifndef LOCAL_ENDPOINT_H
#define LOCAL_ENDPOINT_H
#include <arpa/inet.h>  // or <winsock2.h> for Windows
#include <stdbool.h>

typedef struct {
  sa_family_t family;

  union {
    struct sockaddr_in ipv4_addr;
    struct sockaddr_in6 ipv6_addr;
  } addr;
  bool initialized;
} LocalEndpoint;

void local_endpoint_with_port(LocalEndpoint* remote_endpoint, int port);

#endif  // LOCAL_ENDPOINT_H
