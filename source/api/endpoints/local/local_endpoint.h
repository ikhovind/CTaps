//
// Created by ikhovind on 16.08.25.
//

#ifndef LOCAL_ENDPOINT_H
#define LOCAL_ENDPOINT_H
#include <arpa/inet.h> // or <winsock2.h> for Windows

typedef struct {
    union {
        in_addr_t ipv4_addr; // typically a uint32_t
        struct in6_addr ipv6_addr;
    };
    int port;
} LocalEndpoint;

#endif //LOCAL_ENDPOINT_H
