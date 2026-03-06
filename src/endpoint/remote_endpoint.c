#include "candidate_gathering/candidate_gathering.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "remote_endpoint.h"

#include <endpoint/port_util.h>
#include <endpoint/util.h>
#include <errno.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

void ct_remote_endpoint_build(ct_remote_endpoint_t* remote_endpoint) {
  memset(remote_endpoint, 0, sizeof(ct_remote_endpoint_t));
}

ct_remote_endpoint_t* ct_remote_endpoint_new(void) {
  ct_remote_endpoint_t* endpoint = malloc(sizeof(ct_remote_endpoint_t));
  if (!endpoint) {
    return NULL;
  }
  ct_remote_endpoint_build(endpoint);
  return endpoint;
}

int ct_remote_endpoint_with_ipv4(ct_remote_endpoint_t* remote_endpoint, in_addr_t ipv4_addr) {
  if (remote_endpoint->hostname != NULL) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -EINVAL;
  }
  memset(&remote_endpoint->data.resolved_address, 0, sizeof(struct sockaddr_storage));
  struct sockaddr_in* addr = (struct sockaddr_in*)&remote_endpoint->data.resolved_address;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = ipv4_addr;
  return 0;
}

int ct_remote_endpoint_with_ipv6(ct_remote_endpoint_t* remote_endpoint, struct in6_addr ipv6_addr) {
  if (remote_endpoint->hostname != NULL) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -EINVAL;
  }
  struct sockaddr_in6* addr = (struct sockaddr_in6*)&remote_endpoint->data.resolved_address;
  addr->sin6_family = AF_INET6;
  addr->sin6_addr = ipv6_addr;
  return 0;
}

int ct_remote_endpoint_from_sockaddr(ct_remote_endpoint_t* remote_endpoint, const struct sockaddr_storage* addr) {
  log_trace("Building remote endpoint from sockaddr");
  if (remote_endpoint->hostname != NULL) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -EINVAL;
  }
  if (addr->ss_family == AF_INET) {
    struct sockaddr_in* in_addr = (struct sockaddr_in*)addr;
    remote_endpoint->port = ntohs(in_addr->sin_port);
    memcpy(&remote_endpoint->data.resolved_address, in_addr, sizeof(struct sockaddr_in));
  }
  else if (addr->ss_family == AF_INET6) {
    struct sockaddr_in6* in6_addr = (struct sockaddr_in6*)addr;
    remote_endpoint->port = ntohs(in6_addr->sin6_port);
    memcpy(&remote_endpoint->data.resolved_address, in6_addr, sizeof(struct sockaddr_in6));
  }
  else {
    log_error("Unsupported resolved_address family: %d\n", addr->ss_family);
    return -EINVAL;
  }
  return 0;
}

int ct_remote_endpoint_with_hostname(ct_remote_endpoint_t* remote_endpoint, const char* hostname) {
  if (remote_endpoint->data.resolved_address.ss_family != AF_UNSPEC) {
    log_error("Cannot specify both hostname and IP address on single remote endpoint");
    return -EINVAL;
  }
  remote_endpoint->hostname = (char*) malloc(strlen(hostname) + 1);
  if (!remote_endpoint->hostname) {
    log_error("Could not allocate memory for hostname\n");
    return -errno;
  }
  memcpy(remote_endpoint->hostname, hostname, strlen(hostname) + 1);
  return 0;
}

int ct_remote_endpoint_with_service(ct_remote_endpoint_t* remote_endpoint, const char* service) {
  remote_endpoint->service = strdup(service);
  if (!remote_endpoint->service) {
    log_error("Could not allocate memory for service\n");
    return -ENOMEM;
  }
  return 0;
}

void ct_remote_endpoint_with_port(ct_remote_endpoint_t* remote_endpoint, const uint16_t port) {
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

int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint, ct_remote_resolve_call_context_t* context) {
  log_trace("Resolving remote endpoint");
  int32_t assigned_port = 0;
  if (remote_endpoint->service != NULL) {
    assigned_port = remote_endpoint_get_service_port(remote_endpoint);
  }
  else {
    assigned_port = remote_endpoint->port;
  }
  if (assigned_port == -1) {
    log_error("Could not determine port for remote endpoint with service %s", remote_endpoint->service);
    return -EINVAL;
  }
  if (!context) {
    log_error("ct_remote_endpoint_resolve called with NULL context");
    return -EINVAL;
  }
  context->assigned_port = assigned_port;
  size_t out_count = 0;
  ct_remote_endpoint_t* out_list = NULL;

  if (remote_endpoint->hostname != NULL) {
    log_debug("Endpoint was a hostname, performing DNS lookup for %s", remote_endpoint->hostname);

    int rc = perform_dns_lookup(remote_endpoint->hostname, remote_endpoint->service, context);
    if (rc != 0) {
      log_error("DNS lookup failed for hostname %s with service %s: %s", remote_endpoint->hostname, remote_endpoint->service, strerror(-rc));
      return rc;
    }
  }
  else if (remote_endpoint->data.resolved_address.ss_family != AF_UNSPEC) {
    out_count = 1;
    out_list = malloc(sizeof(ct_remote_endpoint_t));
    if (!out_list) {
      log_error("Could not allocate memory for ct_remote_endpoint_t output list");
      return -ENOMEM;
    }
    memset(out_list, 0, sizeof(ct_remote_endpoint_t));
    memcpy(out_list, remote_endpoint, sizeof(ct_remote_endpoint_t));
    // set port in resolved_address
    if (out_list[0].data.resolved_address.ss_family == AF_INET) {
      struct sockaddr_in* addr = (struct sockaddr_in*)&(out_list)[0].data.resolved_address;
      addr->sin_port = htons(assigned_port);
    }
    else if (out_list[0].data.resolved_address.ss_family == AF_INET6) {
      struct sockaddr_in6* addr = (struct sockaddr_in6*)&(out_list)[0].data.resolved_address;
      addr->sin6_port = htons(assigned_port);
    }
    ct_remote_endpoint_resolve_cb(out_list, out_count, context);
  }
  else {
    log_error("endpoint type was unspecified, cannot resolve\n");
    return -EINVAL;
  }

  return 0;
}

void ct_remote_endpoint_free_strings(ct_remote_endpoint_t* remote_endpoint) {
  if (!remote_endpoint) {
    return;
  }
  if (remote_endpoint->hostname) {
    free(remote_endpoint->hostname);
    remote_endpoint->hostname = NULL;
  }
  if (remote_endpoint->service) {
    free(remote_endpoint->service);
    remote_endpoint->service = NULL;
  }
}

void ct_remote_endpoint_free(ct_remote_endpoint_t* remote_endpoint) {
  ct_remote_endpoint_free_strings(remote_endpoint);
  free(remote_endpoint);
}

int ct_remote_endpoint_copy_content(const ct_remote_endpoint_t* src, ct_remote_endpoint_t* dest) {
  memset(dest, 0, sizeof(ct_remote_endpoint_t));
  *dest = *src;
  dest->service = NULL;
  dest->hostname = NULL;
  if (src->hostname) {
    dest->hostname = strdup(src->hostname);
    if (!dest->hostname) {
      log_error("Could not allocate memory for hostname in copy_content2");
      return -ENOMEM;
    }
  }
  if (src->service) {
    dest->service = strdup(src->service);
    if (!dest->service) {
      log_error("Could not allocate memory for service in copy_content2");
      free(dest->hostname);
      dest->hostname = NULL;
      return -ENOMEM;
    }
  }
  return 0;
}

ct_remote_endpoint_t* ct_remote_endpoint_deep_copy(const ct_remote_endpoint_t* remote_endpoint) {
  ct_remote_endpoint_t* res = malloc(sizeof(ct_remote_endpoint_t));
  int rc = ct_remote_endpoint_copy_content(remote_endpoint, res);
  if (rc != 0) {
    log_error("Failed to copy content for remote endpoint: %s", strerror(-rc));
    free(res);
    return NULL;
  }

  return res;
}

int32_t remote_endpoint_get_service_port(const ct_remote_endpoint_t* remote_endpoint) {
  return get_service_port(ct_remote_endpoint_get_service(remote_endpoint), remote_endpoint->data.resolved_address.ss_family);
}

const char* ct_remote_endpoint_get_service(const ct_remote_endpoint_t* remote_endpoint) {
  return remote_endpoint->service;
}

const struct sockaddr_storage* remote_endpoint_get_resolved_address(const ct_remote_endpoint_t* remote_endpoint) {
  if (!remote_endpoint) {
    log_error("remote_endpoint_get_resolved_address called with NULL parameter");
    return NULL;
  }
  return &remote_endpoint->data.resolved_address;
}

ct_remote_endpoint_t* ct_remote_endpoints_deep_copy(const ct_remote_endpoint_t* remote_endpoints, size_t num_remote_endpoints) {
  if (!remote_endpoints || num_remote_endpoints == 0) {
    return NULL;
  }
  ct_remote_endpoint_t* res = malloc(sizeof(ct_remote_endpoint_t) * num_remote_endpoints);
  if (!res) {
    log_error("Failed to allocate memory for deep copy of remote endpoints");
    return NULL;
  }
  for (size_t i = 0; i < num_remote_endpoints; i++) {
    int rc = ct_remote_endpoint_copy_content(&remote_endpoints[i], &res[i]);
    if (rc != 0) {
      log_error("Failed to copy content for remote endpoint at index %zu: %s", i, strerror(-rc));
      // Free any previously copied endpoints before returning
      for (size_t j = 0; j < i; j++) {
        ct_remote_endpoint_free_strings(&res[j]);
      }
      free(res);
      return NULL;
    }
  }
  return res;
}

void ct_remote_endpoints_free(ct_remote_endpoint_t* remote_endpoints, size_t num_remote_endpoints) {
  for (size_t i = 0; i < num_remote_endpoints; i++) {
    ct_remote_endpoint_free_strings(&remote_endpoints[i]);
  }
  free(remote_endpoints);
}

bool ct_remote_endpoint_resolved_equals(const ct_remote_endpoint_t* endpoint1, const ct_remote_endpoint_t* endpoint2) {
  if (!endpoint1 || !endpoint2) {
    log_warn("ct_remote_endpoint_resolved_equals called with NULL parameter");
    return false;
  }
  return ct_sockaddr_equal(&endpoint1->data.resolved_address, &endpoint2->data.resolved_address);
}
