#ifndef REMOTE_ENDPOINT_H
#define REMOTE_ENDPOINT_H
#include <sys/socket.h>
#include <arpa/inet.h>

/* TODO:
 *   - multicast
 *   - with protocol
 */

typedef struct RemoteEndpoint{
  // host byte order
  uint16_t port;
  char* service;
  char* hostname;
  union {
    struct sockaddr_storage resolved_address;
  } data;
} RemoteEndpoint;

void remote_endpoint_build(RemoteEndpoint* remote_endpoint);

int remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname);

void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, unsigned short port);

int remote_endpoint_from_sockaddr(RemoteEndpoint* remote_endpoint, const struct sockaddr_storage* addr);

int remote_endpoint_with_service(RemoteEndpoint* remote_endpoint,
                                  const char* service);
int remote_endpoint_with_ipv4(RemoteEndpoint* remote_endpoint,
                               in_addr_t ipv4_addr);
int remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint,
                               struct in6_addr ipv6_addr);

int remote_endpoint_resolve(RemoteEndpoint* remote_endpoint, RemoteEndpoint** out_list, size_t* out_count);

#endif  // LOCAL_ENDPOINT_H
