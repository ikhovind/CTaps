#include "local_endpoint.h"

#include "endpoints/util.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <endpoints/port_util.h>

void local_endpoint_with_port(LocalEndpoint* local_endpoint, int port) {
  local_endpoint->port = port;
  if (local_endpoint->data.address.ss_family == AF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint->data.address;
    addr->sin6_port = htons(port);
  }
  if (local_endpoint->data.address.ss_family == AF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.address;
    addr->sin_port = htons(port);
  }
}

void local_endpoint_build(LocalEndpoint* local_endpoint) {
  memset(local_endpoint, 0, sizeof(LocalEndpoint));
}

void local_endpoint_with_ipv4(LocalEndpoint* local_endpoint, in_addr_t ipv4_addr) {
  struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  addr->sin_port = htons(local_endpoint->port);
}

void local_endpoint_with_ipv6(LocalEndpoint* local_endpoint, struct in6_addr ipv6_addr) {
  struct sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint->data.address;
  addr->sin6_family = AF_INET6;
  addr->sin6_addr = ipv6_addr;
  addr->sin6_port = htons(local_endpoint->port);
}

void local_endpoint_with_interface(LocalEndpoint* local_endpoint, char* interface_name) {
  local_endpoint->interface_name = interface_name;
}

int local_endpoint_with_service(LocalEndpoint* local_endpoint, char* service) {
  local_endpoint->service = malloc(strlen(service) + 1);
  if (local_endpoint->service == NULL) {
    return -1;
  }
  strcpy(local_endpoint->service, service);
  return 0;
}

int local_endpoint_resolve(LocalEndpoint* local_endpoint) {
  printf("Resolving local endpoint\n");
  int num_found_addresses = 0;
  struct sockaddr_storage found_interface_addrs[MAX_FOUND_INTERFACE_ADDRS];
  get_interface_addresses(local_endpoint, &num_found_addresses, found_interface_addrs);

  uint16_t assigned_port = 0;
  if (local_endpoint->service != NULL) {
    assigned_port = get_service_port_local(local_endpoint);
  }
  else {
    assigned_port = local_endpoint->port;
  }
  if (num_found_addresses > 0) {
    printf("Addigning address from interface %s\n", local_endpoint->interface_name);
    printf("Assining interface family %d\n", found_interface_addrs[0].ss_family);
    printf("AF_INET is %d\n", AF_INET);
    local_endpoint->data.address = found_interface_addrs[0];
    if (local_endpoint->data.address.ss_family == AF_INET) {
      struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.address;
      addr->sin_port = htons(assigned_port);
    }
    else if (local_endpoint->data.address.ss_family == AF_INET6) {
      struct sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint->data.address;
      addr->sin6_port = htons(assigned_port);
    }
  }
  else {
    printf("Resolving local endpoint to 0.0.0.0:%d\n", assigned_port);
    int rc = uv_ip4_addr("0.0.0.0", assigned_port, (struct sockaddr_in*)&local_endpoint->data.address);
    printf("Rc is: %d\n", rc);
  }
  return 0;
}
