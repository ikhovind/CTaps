#include "util.h"

#include "ctaps.h"
#include "ctaps_internal.h"
#include <endpoint/remote_endpoint.h>
#include <errno.h>
#include <logging/log.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

void get_interface_addresses(const char *interface_name, int *num_found_addresses, struct sockaddr_storage *output_interface_addrs) {
  *num_found_addresses = 0;
  if (interface_name != NULL) {
    uv_interface_address_t* interfaces = NULL;
    int count = 0;
    int rc = uv_interface_addresses(&interfaces, &count);
    if (rc != 0) {
      return;
    }

    for (int i = 0; i < count; i++) {
      if (strcmp("any", interface_name) == 0 || strcmp(interfaces[i].name, interface_name) == 0) {
        if (interfaces[i].address.address4.sin_family == AF_INET) {
          memcpy(&output_interface_addrs[(*num_found_addresses)++], &interfaces[i].address, sizeof(struct sockaddr_in));
        }
        if (interfaces[i].address.address4.sin_family == AF_INET6) {
          memcpy(&output_interface_addrs[(*num_found_addresses)++], &interfaces[i].address, sizeof(struct sockaddr_in6));
        }
        if (*num_found_addresses >= MAX_FOUND_INTERFACE_ADDRS) {
          break;
        }
      }
    }
    uv_free_interface_addresses(interfaces, count);
  }
}

int perform_dns_lookup(const char* hostname, const char* service, ct_remote_endpoint_t** out_list, size_t* out_count, uv_getaddrinfo_cb getaddrinfo_cb) {
  log_debug("Performing dns lookup for hostname: %s\n", hostname);
  uv_getaddrinfo_t request = {0};

  int rc = uv_getaddrinfo(
    event_loop,
    &request,
    getaddrinfo_cb,
    hostname,
    service,
    NULL//&hints
  );

  if (rc < 0) {
    log_error("Could not initiate DNS lookup for hostname %s: %s\n", hostname, uv_strerror(rc));
    return rc;
  }

  *out_count = 0;
  int count = 0;

  for (struct addrinfo* ptr = request.addrinfo; ptr != NULL; ptr = ptr->ai_next) {
    count++;
  }
  log_debug("Found %d addresses for hostname %s", count, hostname);

  if (count == 0) {
    uv_freeaddrinfo(request.addrinfo);
    return 0;
  }
  *out_list = malloc(count * sizeof(ct_remote_endpoint_t));
  if (*out_list == NULL) {
    log_error("Could not allocate memory for ct_remote_endpoint_t output list");
    uv_freeaddrinfo(request.addrinfo);
    return -ENOMEM;
  }

  // Build a single ct_remote_endpoint_t for each resolved address
  for (struct addrinfo* ptr = request.addrinfo; ptr != NULL; ptr = ptr->ai_next) {
    ct_remote_endpoint_t new_node;
    ct_remote_endpoint_build(&new_node);

    if (ptr->ai_family == AF_INET) {
      memcpy(&new_node.data.resolved_address, ptr->ai_addr, sizeof(struct sockaddr_in));
      new_node.port = ntohs(((struct sockaddr_in*)ptr->ai_addr)->sin_port);
    } else if (ptr->ai_family == AF_INET6) {
      memcpy(&new_node.data.resolved_address, ptr->ai_addr, sizeof(struct sockaddr_in6));
      new_node.port = ntohs(((struct sockaddr_in6*)ptr->ai_addr)->sin6_port);
    } else {
      // Skip resolved_address families we don't handle.
      continue;
    }
    (*out_list)[*out_count] = new_node;
    (*out_count)++;
  }

  uv_freeaddrinfo(request.addrinfo);
  return 0;
}
