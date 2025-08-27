#ifndef LOCAL_ENDPOINT_H
#define LOCAL_ENDPOINT_H
#include <arpa/inet.h>  // or <winsock2.h> for Windows
#include <stdbool.h>

typedef enum {
  LOCAL_ENDPOINT_TYPE_UNSPECIFIED,
  LOCAL_ENDPOINT_TYPE_ADDRESS
} LocalEndpointType;

typedef struct {
  sa_family_t family;
  LocalEndpointType type;
  uint16_t port;
  union {
    struct sockaddr_storage address;
  } data;
} LocalEndpoint;

void local_endpoint_build(LocalEndpoint* local_endpoint);
void local_endpoint_with_port(LocalEndpoint* remote_endpoint, int port);
void local_endpoint_with_ipv4(LocalEndpoint* local_endpoint, in_addr_t ipv4_addr);
void local_endpoint_with_ipv6(LocalEndpoint* local_endpoint, struct in6_addr ipv6_addr);

#endif  // LOCAL_ENDPOINT_H
