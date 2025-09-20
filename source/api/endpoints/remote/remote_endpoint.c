#include "remote_endpoint.h"

#include <endpoints/port_util.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <endpoints/util.h>
#include <logging/log.h>

void remote_endpoint_build(RemoteEndpoint* remote_endpoint) {
  memset(remote_endpoint, 0, sizeof(RemoteEndpoint));
}

int remote_endpoint_with_ipv4(RemoteEndpoint* remote_endpoint, in_addr_t ipv4_addr) {
  if (remote_endpoint->hostname != NULL) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -1;
  }
  struct sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.resolved_address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  return 0;
}

int remote_endpoint_with_ipv6(RemoteEndpoint* remote_endpoint, struct in6_addr ipv6_addr) {
  if (remote_endpoint->hostname != NULL) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -1;
  }
  struct sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.resolved_address;
  addr->sin6_family = AF_INET6;
  addr->sin6_addr = ipv6_addr;
  return 0;
}

int remote_endpoint_from_sockaddr(RemoteEndpoint* remote_endpoint, const struct sockaddr_storage* addr) {
  if (remote_endpoint->hostname != NULL) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -1;
  }
  if (addr->ss_family == AF_INET) {
    struct sockaddr_in* in_addr = (struct sockaddr_in*)addr;
    remote_endpoint->port = ntohs(in_addr->sin_port);
    remote_endpoint->data.resolved_address = *((struct sockaddr_storage*)addr);
  }
  else if (addr->ss_family == AF_INET6) {
    struct sockaddr_in6* in6_addr = (struct sockaddr_in6*)addr;
    remote_endpoint->port = ntohs(in6_addr->sin6_port);
    remote_endpoint->data.resolved_address = *((struct sockaddr_storage*)addr);
  }
  else {
    log_error("Unsupported resolved_address family: %d\n", addr->ss_family);
    return -1;
  }
  return 0;
}

int remote_endpoint_with_hostname(RemoteEndpoint* remote_endpoint, const char* hostname) {
  if (remote_endpoint->data.resolved_address.ss_family != AF_UNSPEC) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -1;
  }
  remote_endpoint->hostname = (char*) malloc(strlen(hostname) + 1);
  if (remote_endpoint->hostname == NULL) {
    log_error("Could not allocate memory for hostname\n");
    return -1;
  }
  printf("About to memcpy\n");
  memcpy(remote_endpoint->hostname, hostname, strlen(hostname) + 1);
  return 0;
}

int remote_endpoint_with_service(RemoteEndpoint* remote_endpoint, const char* service) {
  remote_endpoint->service = malloc(strlen(service) + 1);
  if (remote_endpoint->service == NULL) {
    log_error("Could not allocate memory for service\n");
    return -1;
  }
  strcpy(remote_endpoint->service, service);
  return 0;
}

void remote_endpoint_with_port(RemoteEndpoint* remote_endpoint, const uint16_t port) {
  remote_endpoint->port = port;
  if (remote_endpoint->data.resolved_address.ss_family == AF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.resolved_address;
    addr->sin6_port = htons(port);
  }
  if (remote_endpoint->data.resolved_address.ss_family == AF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.resolved_address;
    addr->sin_port = htons(port);
  }
}

int remote_endpoint_resolve(RemoteEndpoint* remote_endpoint, RemoteEndpoint** out_list, size_t* out_count) {
  printf("Resolving remote endpoint\n");
  int32_t assigned_port = 0;
  if (remote_endpoint->service != NULL) {
    printf("Service was not null\n");
    assigned_port = get_service_port_remote(remote_endpoint);
  }
  else {
    printf("Service was null, setting to port: %d\n", remote_endpoint->port);
    assigned_port = remote_endpoint->port;
  }
  printf("Assigned port is : %d\n", assigned_port);

  if (remote_endpoint->hostname != NULL) {
    log_debug("Endpoint was a hostname, performing DNS lookup\n");
    perform_dns_lookup(remote_endpoint->hostname, NULL, out_list, out_count, NULL);
    for (int i = 0; i < *out_count; i++) {
      (*out_list)[i].port = assigned_port;
      // set port in resolved_address
      if ((*out_list)[i].data.resolved_address.ss_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)&(*out_list)[i].data.resolved_address;
        addr->sin_port = htons(assigned_port);
      }
      else if ((*out_list)[i].data.resolved_address.ss_family == AF_INET6) {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&(*out_list)[i].data.resolved_address;
        addr->sin6_port = htons(assigned_port);
      }
    }
    log_debug("Successfully performed DNS lookup, found %zu addresses\n", *out_count);
  }
  else if (remote_endpoint->data.resolved_address.ss_family != AF_UNSPEC) {
    log_debug("Endpoint was an IP address");
    *out_count = 1;
    *out_list = malloc(sizeof(RemoteEndpoint));
    remote_endpoint_build(&(*out_list)[0]);
    memcpy(*out_list, remote_endpoint, sizeof(RemoteEndpoint));
    // set port in resolved_address
    if (out_list[0]->data.resolved_address.ss_family == AF_INET) {
      struct sockaddr_in* addr = (struct sockaddr_in*)&(*out_list)[0].data.resolved_address;
      addr->sin_port = htons(assigned_port);
    }
    else if (out_list[0]->data.resolved_address.ss_family == AF_INET6) {
      struct sockaddr_in6* addr = (struct sockaddr_in6*)&(*out_list)[0].data.resolved_address;
      addr->sin6_port = htons(assigned_port);
    }
  }
  else {
    printf("endpoint type was unspecified, cannot resolve\n");
    return -1;
  }
  return 0;
}
