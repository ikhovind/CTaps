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
        if (interfaces[i].address.address6.sin6_family == AF_INET6) {
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
  log_debug("DNS lookup completed with status: %d", status);
  ct_remote_resolve_call_context_t* context = req->data;
  ct_candidate_node_t* parent_data = (ct_candidate_node_t*)context->parent_node->data;
  if (status < 0) {
    log_error("DNS lookup failed for hostname %s: %s", parent_data->remote_endpoint->hostname, uv_strerror(status));
    ct_remote_endpoint_resolve_cb(NULL, 0, context);
    free(req);
    return;
  }

  ct_remote_endpoint_t* out_list = NULL;

  size_t out_count = 0;
  // Build a single ct_remote_endpoint_t for each resolved address
  for (struct addrinfo* ptr = res; ptr != NULL; ptr = ptr->ai_next) {
    if (ptr->ai_family != AF_INET && ptr->ai_family != AF_INET6) {
        continue;
    }
    // realloc temporary variable, so that we can still free out_list if it fails
    ct_remote_endpoint_t* tmp = realloc(out_list, (out_count + 1) * sizeof(ct_remote_endpoint_t));
    if (!tmp) {
        log_error("Could not allocate memory for ct_remote_endpoint_t output list");
        free(out_list);
        ct_remote_endpoint_resolve_cb(NULL, 0, context);
        uv_freeaddrinfo(req->addrinfo);
        free(req);
        return;
    }
    out_list = tmp;

    ct_remote_endpoint_t* new_node = &out_list[out_count];
    ct_remote_endpoint_build(new_node);
    new_node->port = context->assigned_port;

    if (ptr->ai_family == AF_INET) {
      memcpy(&new_node->data.resolved_address, ptr->ai_addr, sizeof(struct sockaddr_in));
      ((struct sockaddr_in*)&new_node->data.resolved_address)->sin_port = htons(context->assigned_port);
    } else {
      memcpy(&new_node->data.resolved_address, ptr->ai_addr, sizeof(struct sockaddr_in6));
      ((struct sockaddr_in6*)&new_node->data.resolved_address)->sin6_port = htons(context->assigned_port);
    } 
    out_count++;
  }
  ct_remote_endpoint_resolve_cb(out_list, out_count, context);
  uv_freeaddrinfo(req->addrinfo);
  free(req);
}

int perform_dns_lookup(const char* hostname, const char* service, ct_remote_resolve_call_context_t* context) {
  log_trace("Performing dns lookup for hostname: %s\n", hostname);
  uv_getaddrinfo_t* request = calloc(1, sizeof(uv_getaddrinfo_t));
  if (!request) {
    log_error("Could not allocate memory for uv_getaddrinfo_t request");
    return -ENOMEM;
  }
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
