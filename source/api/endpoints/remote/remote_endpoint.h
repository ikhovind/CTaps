#ifndef REMOTE_ENDPOINT_H
#define REMOTE_ENDPOINT_H
#include <sys/socket.h>
#include <arpa/inet.h>

/* TODO:
 *   - multicast
 *   - with protocol
 */

typedef struct ct_remote_endpoint_t{
  // host byte order
  uint16_t port;
  char* service;
  char* hostname;
  union {
    struct sockaddr_storage resolved_address;
  } data;
} ct_remote_endpoint_t;

void ct_remote_endpoint_build(ct_remote_endpoint_t* remote_endpoint);

int ct_remote_endpoint_with_hostname(ct_remote_endpoint_t* remote_endpoint, const char* hostname);

void ct_remote_endpoint_with_port(ct_remote_endpoint_t* remote_endpoint, unsigned short port);

int ct_remote_endpoint_from_sockaddr(ct_remote_endpoint_t* remote_endpoint, const struct sockaddr_storage* addr);

int ct_remote_endpoint_with_service(ct_remote_endpoint_t* remote_endpoint,
                                  const char* service);
int ct_remote_endpoint_with_ipv4(ct_remote_endpoint_t* remote_endpoint,
                               in_addr_t ipv4_addr);
int ct_remote_endpoint_with_ipv6(ct_remote_endpoint_t* remote_endpoint,
                               struct in6_addr ipv6_addr);

int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint, ct_remote_endpoint_t** out_list, size_t* out_count);

void ct_free_remote_endpoint(ct_remote_endpoint_t* remote_endpoint);

void ct_free_remote_endpoint_strings(ct_remote_endpoint_t* remote_endpoint);

ct_remote_endpoint_t* remote_endpoint_copy(const ct_remote_endpoint_t* remote_endpoint);

ct_remote_endpoint_t ct_remote_endpoint_copy_content(const ct_remote_endpoint_t* remote_endpoint);

#endif  // LOCAL_ENDPOINT_H
