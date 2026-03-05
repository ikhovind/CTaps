#include "ctaps.h"
#include "ctaps_internal.h"
#include "local_endpoint.h"

#include "endpoint/util.h"
#include <endpoint/port_util.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

void ct_local_endpoint_with_port(ct_local_endpoint_t* local_endpoint, int port) {
  local_endpoint->port = port;
  if (local_endpoint->data.resolved_address.ss_family == AF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)&local_endpoint->data.resolved_address;
    addr->sin6_port = htons(port);
  }
  if (local_endpoint->data.resolved_address.ss_family == AF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.resolved_address;
    addr->sin_port = htons(port);
  }
}

char* local_endpoint_get_interface_name(const ct_local_endpoint_t* local_endpoint) {
  return local_endpoint->interface_name;
}

void ct_local_endpoint_build(ct_local_endpoint_t* local_endpoint) {
  memset(local_endpoint, 0, sizeof(ct_local_endpoint_t));
}

ct_local_endpoint_t* ct_local_endpoint_new(void) {
  ct_local_endpoint_t* endpoint = malloc(sizeof(ct_local_endpoint_t));
  if (!endpoint) {
    return NULL;
  }
  ct_local_endpoint_build(endpoint);
  return endpoint;
}

int ct_local_endpoint_with_interface(ct_local_endpoint_t* local_endpoint, const char* interface_name) {
  log_trace("Allocating %zu bytes of memory for interface name", strlen(interface_name) + 1);
  local_endpoint->interface_name = strdup(interface_name);
  return 0;
}

int ct_local_endpoint_with_service(ct_local_endpoint_t* local_endpoint, char* service) {
  local_endpoint->service = strdup(service);
  if (!local_endpoint->service) {
    return -ENOMEM;
  }
  return 0;
}

ct_local_endpoint_t* ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, size_t* out_count) {
  if (!out_count) {
    log_error("Output count pointer was NULL, cannot resolve local endpoint");
    return NULL;
  }
  if (!local_endpoint) {
    log_error("Local endpoint pointer was NULL, cannot resolve local endpoint");
    *out_count = 0;
    return NULL;
  }
  int num_found_addresses = 0;
  ct_local_endpoint_t* out_list = NULL;
  *out_count = 0;
  struct sockaddr_storage found_interface_addrs[MAX_FOUND_INTERFACE_ADDRS] = {0};
  if (!local_endpoint->interface_name) {
    log_debug("Interface name was NULL, getting addresses for 'any' interface");
    get_interface_addresses("any", &num_found_addresses, found_interface_addrs);
  }
  else {
    log_debug("Interface name was not NULL, getting addresses for '%s' interface", local_endpoint->interface_name);
    get_interface_addresses(local_endpoint->interface_name, &num_found_addresses, found_interface_addrs);
  }
  log_debug("Found %d addresses for interface %s", num_found_addresses, local_endpoint->interface_name ? local_endpoint->interface_name : "any");

  uint16_t assigned_port = 0;
  if (local_endpoint->service != NULL) {
    log_trace("Service was not NULL, resolving service to port");
    assigned_port = local_endpoint_get_service_port(local_endpoint);
    log_trace("Resolved service to port: %d", assigned_port);
  }
  else {
    log_trace("Service was NULL, using port: %d", local_endpoint->port);
    assigned_port = local_endpoint->port;
  }
  if (num_found_addresses > 0) {
    log_debug("Found %d interface addresses", num_found_addresses);
    out_list = calloc(num_found_addresses, sizeof(ct_local_endpoint_t));
    if (!out_list) {
      log_error("Could not allocate memory for output list of local endpoints");
      return NULL;
    }
    *out_count = num_found_addresses;

    for (int i = 0; i < num_found_addresses; i++) {
      struct sockaddr_storage* sockaddr_storage = &found_interface_addrs[i];
      out_list[i].port = assigned_port;
      out_list[i].interface_name = local_endpoint->interface_name ? strdup(local_endpoint->interface_name) : NULL;
      out_list[i].service = local_endpoint->service ? strdup(local_endpoint->service) : NULL;
      out_list[i].data.resolved_address = *sockaddr_storage;
      if (sockaddr_storage->ss_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)&out_list[i].data.resolved_address;
        addr->sin_port = htons(assigned_port);
      }
      if (sockaddr_storage->ss_family == AF_INET6) {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&out_list[i].data.resolved_address;
        addr->sin6_port = htons(assigned_port);
      }
    }
  }
  return out_list;
}

void ct_local_endpoint_free_strings(ct_local_endpoint_t* local_endpoint) {
  if (!local_endpoint) {
    return;
  }
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

void ct_local_endpoint_free(ct_local_endpoint_t* local_endpoint) {
  if (!local_endpoint) {
    return;
  }
  log_trace("Freeing local endpoint");
  ct_local_endpoint_free_strings(local_endpoint);
  free(local_endpoint);
}

ct_local_endpoint_t* ct_local_endpoint_deep_copy(const ct_local_endpoint_t* local_endpoint) {
  if (!local_endpoint) {
    log_error("Cannot deep copy NULL local endpoint");
    return NULL;
  }
  ct_local_endpoint_t* res = malloc(sizeof(ct_local_endpoint_t));
  if (!res) {
    log_error("Failed to allocate memory for local endpoint copy");
    return NULL;
  }
  memset(res, 0, sizeof(ct_local_endpoint_t));
  int rc = ct_local_endpoint_copy_content(local_endpoint, res);
  if (rc != 0) {
    log_error("Failed to copy local endpoint content for deep copy: %s", strerror(-rc));
    free(res);
    return NULL;
  }
  return res;
}

int32_t local_endpoint_get_service_port(const ct_local_endpoint_t* local_endpoint) {
  return get_service_port(ct_local_endpoint_get_service(local_endpoint), local_endpoint->data.resolved_address.ss_family);
}

const char* ct_local_endpoint_get_service(const ct_local_endpoint_t* local_endpoint) {
  return local_endpoint->service;
}

const struct sockaddr_storage* local_endpoint_get_resolved_address(const ct_local_endpoint_t* local_endpoint) {
  if (!local_endpoint) {
    return NULL;
  }
  return &local_endpoint->data.resolved_address;
}

sa_family_t local_endpoint_get_address_family(const ct_local_endpoint_t* local_endpoint) {
  if (!local_endpoint) {
    return 0;
  }
  return local_endpoint_get_resolved_address(local_endpoint)->ss_family;
}

uint16_t local_endpoint_get_resolved_port(const ct_local_endpoint_t* local_endpoint) {
  const struct sockaddr_storage* resolved_addr = local_endpoint_get_resolved_address(local_endpoint);
  if (!resolved_addr) {
    return 0;
  }
  if (local_endpoint_get_address_family(local_endpoint) == AF_INET6) {
    return ((struct sockaddr_in6*)local_endpoint_get_resolved_address(local_endpoint))->sin6_port;
  }
  if (local_endpoint_get_address_family(local_endpoint) == AF_INET) {
    return ((struct sockaddr_in*)local_endpoint_get_resolved_address(local_endpoint))->sin_port;
  }
  return 0;
}

void local_endpoint_set_resolved_address(ct_local_endpoint_t* local_endpoint, const struct sockaddr_storage* resolved_address) {
  if (!local_endpoint || !resolved_address) {
    log_error("Cannot set resolved address on NULL local endpoint or with NULL resolved address");
    log_debug("local_endpoint: %p, resolved_address: %p", (void*)local_endpoint, (void*)resolved_address);
    return;
  }
  local_endpoint->data.resolved_address = *resolved_address;
}

int ct_local_endpoint_copy_content(const ct_local_endpoint_t* src, ct_local_endpoint_t* dest) {
  if (!src || !dest) {
    log_error("Cannot copy local endpoint content from or to NULL pointer");
    return -EINVAL;
  }
  log_debug("src->data.resolved_address.ss_family: %d", src->data.resolved_address.ss_family);
  *dest = *src;
  dest->service = NULL;
  dest->interface_name = NULL;
  if (src->service) {
    dest->service = strdup(src->service);
    if (!dest->service) {
      log_error("Failed to allocate memory for local endpoint service copy");
      return -ENOMEM;
    }
  }
  if (src->interface_name) {
    dest->interface_name = strdup(src->interface_name);
    if (!dest->interface_name) {
      log_error("Failed to allocate memory for local endpoint interface name copy");
      free(dest->service);
      return -ENOMEM;
    }
  }
  return 0;
}

ct_local_endpoint_t* ct_local_endpoints_deep_copy(const ct_local_endpoint_t* local_endpoints, size_t num_local_endpoints) {
  if (!local_endpoints || num_local_endpoints == 0) {
    return NULL;
  }
  ct_local_endpoint_t* res = malloc(sizeof(ct_local_endpoint_t) * num_local_endpoints);
  if (!res) {
    log_error("Failed to allocate memory for deep copy of local endpoints");
    return NULL;
  }
  for (size_t i = 0; i < num_local_endpoints; i++) {
    int rc = ct_local_endpoint_copy_content(&local_endpoints[i], &res[i]);
    if (rc != 0) {
      log_error("Failed to copy content for local endpoint at index %zu: %s", i, strerror(-rc));
      // Free any previously copied endpoints before returning
      for (size_t j = 0; j < i; j++) {
        ct_local_endpoint_free_strings(&res[j]);
      }
      free(res);
      return NULL;
    }
  }
  return res;
}

void ct_local_endpoints_free(ct_local_endpoint_t* local_endpoints, size_t num_local_endpoints) {
  for (size_t i = 0; i < num_local_endpoints; i++) {
    ct_local_endpoint_free_strings(&local_endpoints[i]);
  }
  free(local_endpoints);
}

int ct_local_endpoint_with_ipv4(ct_local_endpoint_t* local_endpoint, in_addr_t ipv4_addr) {
  if (!local_endpoint) {
    log_error("ct_local_endpoint_with_ipv4 called with NULL local endpoint");
    return -EINVAL;
  }
  memset(&local_endpoint->data.resolved_address, 0, sizeof(struct sockaddr_storage));
  struct sockaddr_in* addr = (struct sockaddr_in*)&local_endpoint->data.resolved_address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  return 0;
}
