#include "local_endpoint.h"

#include "endpoints/util.h"

#include <endpoints/port_util.h>
#include <errno.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int local_endpoint_with_interface(LocalEndpoint* local_endpoint, char* interface_name) {
  local_endpoint->interface_name = interface_name;
  local_endpoint->interface_name = malloc(strlen(interface_name) + 1);
  if (local_endpoint->interface_name == NULL) {
    return -errno;
  }
  strcpy(local_endpoint->interface_name, interface_name);
  return 0;
}

int local_endpoint_with_service(LocalEndpoint* local_endpoint, char* service) {
  local_endpoint->service = malloc(strlen(service) + 1);
  if (local_endpoint->service == NULL) {
    return -errno;
  }
  strcpy(local_endpoint->service, service);
  return 0;
}

int local_endpoint_resolve(const LocalEndpoint* local_endpoint, LocalEndpoint** out_list, size_t* out_count) {
  log_info("Resolving local endpoint");
  int num_found_addresses = 0;
  *out_count = 0;
  struct sockaddr_storage found_interface_addrs[MAX_FOUND_INTERFACE_ADDRS];
  if (local_endpoint->interface_name == NULL) {
    log_debug("Interface name was NULL, getting addresses for 'any' interface");
    get_interface_addresses("any", &num_found_addresses, found_interface_addrs);
  }
  else {
    log_debug("Interface name was not NULL, getting addresses for '%s' interface", local_endpoint->interface_name);
    get_interface_addresses(local_endpoint->interface_name, &num_found_addresses, found_interface_addrs);
  }
  log_trace("Found %d addresses for interface %s", num_found_addresses, local_endpoint->interface_name ? local_endpoint->interface_name : "any");

  uint16_t assigned_port = 0;
  if (local_endpoint->service != NULL) {
    log_trace("Service was not NULL, resolving service to port");
    assigned_port = get_service_port_local(local_endpoint);
    log_trace("Resolved service to port: %d", assigned_port);
  }
  else {
    log_trace("Service was NULL, using port: %d", local_endpoint->port);
    assigned_port = local_endpoint->port;
  }
  if (num_found_addresses > 0) {
    log_debug("Found %d interface addresses", num_found_addresses);
    *out_list = malloc(sizeof(LocalEndpoint) * num_found_addresses);
    *out_count = num_found_addresses;

    for (int i = 0; i < num_found_addresses; i++) {
      struct sockaddr_storage* sockaddr_storage = &found_interface_addrs[i];
      local_endpoint_build(&(*out_list)[i]);
      (*out_list)[i].port = assigned_port;
      (*out_list)[i].interface_name = local_endpoint->interface_name ? strdup(local_endpoint->interface_name) : NULL;
      (*out_list)[i].service = local_endpoint->service ? strdup(local_endpoint->service) : NULL;
      (*out_list)[i].data.address = *sockaddr_storage;
      if (sockaddr_storage->ss_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)&(*out_list)[i].data.address;
        addr->sin_port = htons(assigned_port);
      }
      if (sockaddr_storage->ss_family == AF_INET6) {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&(*out_list)[i].data.address;
        addr->sin6_port = htons(assigned_port);
      }
    }
  }
  return 0;
}
