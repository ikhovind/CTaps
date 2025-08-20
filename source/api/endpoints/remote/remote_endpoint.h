//
// Created by ikhovind on 16.08.25.
//

#ifndef REMOTE_ENDPOINT_H
#define REMOTE_ENDPOINT_H
#include <arpa/inet.h>

typedef struct {
    sa_family_t family;

    union {
        struct sockaddr_in ipv4_addr;
        struct sockaddr_in6 ipv6_addr;
    } addr;
} RemoteEndpoint;

void remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname);
void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, int port);
void remote_endpoint_with_service(RemoteEndpoint* remote_endpoint, const char* service);
void remote_endpoint_with_ipv4(RemoteEndpoint* remote_endpoint, in_addr_t ipv4_addr);
void remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint, struct in6_addr ipv6_addr);
void remote_endpoint_with_interface(RemoteEndpoint* remote_endpoint, const char* hostname);
// TODO - remote endpoint with multicast

#endif //LOCAL_ENDPOINT_H
