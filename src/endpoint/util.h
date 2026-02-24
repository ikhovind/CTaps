#ifndef UTIL_H
#define UTIL_H
#include "candidate_gathering/candidate_gathering.h"
#define MAX_FOUND_INTERFACE_ADDRS 64

#include "ctaps.h"
#include <uv.h>

typedef struct ct_dns_lookup_callbacks_s {
  void (*dns_lookup_complete_cb)(ct_remote_endpoint_t* remote_endpoint, size_t out_count, void* context);
  void* context;
} ct_dns_lookup_callbacks_t;

void get_interface_addresses(const char *interface_name, int *num_found_addresses, struct sockaddr_storage *output_interface_addrs);

int perform_dns_lookup(const char* hostname, const char* service, ct_remote_resolve_call_context_t* context);

#endif //UTIL_H
