#ifndef LOCAL_ENDPOINT_H
#define LOCAL_ENDPOINT_H
#include <arpa/inet.h>  // or <winsock2.h> for Windows
#include <stdbool.h>

/* TODO:
 *   - with service?
 *   - multicast
 *   - with protocol
 */

typedef struct {
  uint16_t port;
  char* interface_name;
  char* service;
  union {
    struct sockaddr_storage address;
  } data;
} LocalEndpoint;

void local_endpoint_build(LocalEndpoint* local_endpoint);
void local_endpoint_with_port(LocalEndpoint* local_endpoint, int port);
void local_endpoint_with_interface(LocalEndpoint* local_endpoint, char* interface_name);
void local_endpoint_with_service(LocalEndpoint* local_endpoint, char* service);
int local_endpoint_resolve(LocalEndpoint* local_endpoint);

#endif  // LOCAL_ENDPOINT_H
