#include "ctaps.h"

#include "endpoint/util.h"
#include <endpoint/port_util.h>
#include <errno.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

void ct_local_endpoint_with_port(ct_local_endpoint_t* local_endpoint, int port) {
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

void ct_local_endpoint_build(ct_local_endpoint_t* local_endpoint) {
  memset(local_endpoint, 0, sizeof(ct_local_endpoint_t));
}

int ct_local_endpoint_with_interface(ct_local_endpoint_t* local_endpoint, const char* interface_name) {
  log_trace("Allocating %zu bytes of memory for interface name", strlen(interface_name) + 1);
  local_endpoint->interface_name = strdup(interface_name);
  return 0;
}

int ct_local_endpoint_with_service(ct_local_endpoint_t* local_endpoint, char* service) {
  local_endpoint->service = strdup(service);
  if (local_endpoint->service == NULL) {
    return -ENOMEM;
  }
  return 0;
}

int ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, ct_local_endpoint_t** out_list, size_t* out_count) {
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
    *out_list = malloc(sizeof(ct_local_endpoint_t) * num_found_addresses);
    *out_count = num_found_addresses;

    for (int i = 0; i < num_found_addresses; i++) {
      struct sockaddr_storage* sockaddr_storage = &found_interface_addrs[i];
      ct_local_endpoint_build(&(*out_list)[i]);
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

void ct_free_local_endpoint_strings(ct_local_endpoint_t* local_endpoint) {
  if (local_endpoint->interface_name) {
    log_trace("Freeing local endpoint interface name");
    free(local_endpoint->interface_name);
    local_endpoint->interface_name = NULL;
    log_trace("local endpoint interface after freeing is: %p", local_endpoint->interface_name);
  }
  if (local_endpoint->service) {
    log_trace("Freeing local endpoint service name");
    free(local_endpoint->service);
    local_endpoint->service = NULL;
  }
}

void ct_free_local_endpoint(ct_local_endpoint_t* local_endpoint) {
  ct_free_local_endpoint_strings(local_endpoint);
  free(local_endpoint);
}

ct_local_endpoint_t ct_local_endpoint_copy_content(const ct_local_endpoint_t* local_endpoint) {
  ct_local_endpoint_t res = {0};
  res = *local_endpoint;

  if (local_endpoint->interface_name) {
    res.interface_name = strdup(local_endpoint->interface_name);
  }
  if (local_endpoint->service) {
    res.service = strdup(local_endpoint->service);
  }
  return res;
}

ct_local_endpoint_t* local_endpoint_copy(const ct_local_endpoint_t* local_endpoint) {
  ct_local_endpoint_t* res = malloc(sizeof(ct_local_endpoint_t));
  *res = ct_local_endpoint_copy_content(local_endpoint);
  return res;
}
