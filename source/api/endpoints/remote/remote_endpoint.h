//
// Created by ikhovind on 16.08.25.
//

#ifndef REMOTE_ENDPOINT_H
#define REMOTE_ENDPOINT_H
#include <uv.h>
#include <arpa/inet.h>

typedef enum {
  ENDPOINT_TYPE_UNSPECIFIED,
  ENDPOINT_TYPE_HOSTNAME,
  ENDPOINT_TYPE_ADDRESS
} EndpointType;


typedef struct RemoteEndpoint{
  EndpointType type;
  uint16_t port;
  union {
    struct sockaddr_storage address;
    char* hostname;
  } data;
} RemoteEndpoint;

void remote_endpoint_build(RemoteEndpoint* remote_endpoint);

int remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname);

void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, unsigned short port);

void remote_endpoint_with_service(RemoteEndpoint* remote_endpoint,
                                  const char* service);
void remote_endpoint_with_ipv4(RemoteEndpoint* remote_endpoint,
                               in_addr_t ipv4_addr);
void remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint,
                               struct in6_addr ipv6_addr);
// TODO - remote endpoint with multicast

#endif  // LOCAL_ENDPOINT_H
