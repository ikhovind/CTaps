#ifndef LOCAL_ENDPOINT_H
#define LOCAL_ENDPOINT_H
#include <arpa/inet.h>  // or <winsock2.h> for Windows
#include <stdbool.h>

/* TODO:
 *   - with service?
 *   - with interface
 *   - multicast
 *   - with protocol
 */

typedef struct {
  sa_family_t family;
  uint16_t port;
  char* interface_name;
  union {
    struct sockaddr_storage address;
  } data;
} LocalEndpoint;

void local_endpoint_build(LocalEndpoint* local_endpoint);
void local_endpoint_with_port(LocalEndpoint* remote_endpoint, int port);
void local_endpoint_with_interface(LocalEndpoint* local_endpoint, char* interface_name);

#endif  // LOCAL_ENDPOINT_H
