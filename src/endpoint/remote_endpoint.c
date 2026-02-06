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
  if (remote_endpoint->hostname == NULL) {
    log_error("Could not allocate memory for hostname\n");
    return -errno;
  }
  memcpy(remote_endpoint->hostname, hostname, strlen(hostname) + 1);
  return 0;
}

int ct_remote_endpoint_with_service(ct_remote_endpoint_t* remote_endpoint, const char* service) {
  remote_endpoint->service = strdup(service);
  if (remote_endpoint->service == NULL) {
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

int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint, ct_remote_endpoint_t** out_list, size_t* out_count) {
  log_trace("Resolving remote endpoint");
  int32_t assigned_port = 0;
  if (remote_endpoint->service != NULL) {
    assigned_port = remote_endpoint_get_service_port(remote_endpoint);
  }
  else {
    assigned_port = remote_endpoint->port;
  }

  if (remote_endpoint->hostname != NULL) {
    log_debug("Endpoint was a hostname, performing DNS lookup for %s", remote_endpoint->hostname);
    perform_dns_lookup(remote_endpoint->hostname, NULL, out_list, out_count, NULL);
    for (size_t i = 0; i < *out_count; i++) {
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
    log_debug("Successfully performed DNS lookup, found %zu addresses", *out_count);
  }
  else if (remote_endpoint->data.resolved_address.ss_family != AF_UNSPEC) {
    log_debug("Endpoint was an IP address");
    *out_count = 1;
    *out_list = malloc(sizeof(ct_remote_endpoint_t));
    ct_remote_endpoint_build(&(*out_list)[0]);
    memcpy(*out_list, remote_endpoint, sizeof(ct_remote_endpoint_t));
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

ct_remote_endpoint_t ct_remote_endpoint_copy_content(const ct_remote_endpoint_t* remote_endpoint) {
  ct_remote_endpoint_t res = {0};
  res = *remote_endpoint;
  if (remote_endpoint->hostname) {
    res.hostname = strdup(remote_endpoint->hostname);
  }
  if (remote_endpoint->service) {
    res.service = strdup(remote_endpoint->service);
  }
  return res;
}

ct_remote_endpoint_t* ct_remote_endpoint_deep_copy(const ct_remote_endpoint_t* remote_endpoint) {
  ct_remote_endpoint_t* res = malloc(sizeof(ct_remote_endpoint_t));
  *res = ct_remote_endpoint_copy_content(remote_endpoint);
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
