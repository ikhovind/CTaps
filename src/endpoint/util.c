#include "util.h"

#include "candidate_gathering/candidate_gathering.h"
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

void on_uv_getaddrinfo_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
  (void)res;  // We access the results through req->addrinfo, so we don't need this parameter.
  (void)status;  // We handle errors through the status parameter, but we don't need to access it after that.
  log_debug("DNS lookup completed with status: %d", status);
  int count = 0;
  ct_remote_resolve_call_context_t* context = req->data;

  for (struct addrinfo* ptr = req->addrinfo; ptr != NULL; ptr = ptr->ai_next) {
    count++;
  }
  log_debug("Number of addresses resolved: %d", count);
  for (struct addrinfo* ptr = res; ptr != NULL; ptr = ptr->ai_next) {
    log_debug("Resolved address family: %d", ptr->ai_family);
  }

  if (count == 0) {
    ct_remote_endpoint_resolve_cb(NULL, 0, context);
    uv_freeaddrinfo(req->addrinfo);
    free(req);
    return;
  }
  ct_remote_endpoint_t* out_list = calloc(count, sizeof(ct_remote_endpoint_t));
  if (!out_list) {
    log_error("Could not allocate memory for ct_remote_endpoint_t output list");
    ct_remote_endpoint_resolve_cb(NULL, 0, context);
    uv_freeaddrinfo(req->addrinfo);
    free(req);
    return;
  }

  size_t out_count = 0;
  // Build a single ct_remote_endpoint_t for each resolved address
  for (struct addrinfo* ptr = req->addrinfo; ptr != NULL; ptr = ptr->ai_next) {
    ct_remote_endpoint_t new_node;
    ct_remote_endpoint_build(&new_node);
    new_node.port = context->assigned_port;

    if (ptr->ai_family == AF_INET) {
      memcpy(&new_node.data.resolved_address, ptr->ai_addr, sizeof(struct sockaddr_in));
      ((struct sockaddr_in*)&new_node.data.resolved_address)->sin_port = htons(context->assigned_port);
    } else if (ptr->ai_family == AF_INET6) {
      memcpy(&new_node.data.resolved_address, ptr->ai_addr, sizeof(struct sockaddr_in6));
      ((struct sockaddr_in6*)&new_node.data.resolved_address)->sin6_port = htons(context->assigned_port);
    } else {
      // Skip resolved_address families we don't handle.
      continue;
    }
    out_list[out_count] = new_node;
    out_count++;
  }
  ct_remote_endpoint_resolve_cb(out_list, out_count, context);
  uv_freeaddrinfo(req->addrinfo);
  free(req);
}

int perform_dns_lookup(const char* hostname, const char* service, ct_remote_resolve_call_context_t* context) {
  log_trace("Performing dns lookup for hostname: %s\n", hostname);
  uv_getaddrinfo_t* request = calloc(1, sizeof(uv_getaddrinfo_t));
  request->data = context;


  int rc = uv_getaddrinfo(
    event_loop,
    request,
    on_uv_getaddrinfo_cb,
    hostname,
    service,
    NULL
  );
  if (rc < 0) {
    log_error("Synchronous error in initiating DNS lookup for hostname %s: %s\n", hostname, uv_strerror(rc));
    free(request);
    return rc;
  }
  return 0;
}
