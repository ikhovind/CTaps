#ifndef UTIL_H
#define UTIL_H
#include "candidate_gathering/candidate_gathering.h"
#define MAX_FOUND_INTERFACE_ADDRS 64

#include <uv.h>

void ct_get_interface_addresses(const char* interface_name, int* num_found_addresses,
                             struct sockaddr_storage* output_interface_addrs);

int ct_perform_dns_lookup(const char* hostname, const char* service,
                       ct_remote_resolve_call_context_t* context);

bool ct_sockaddr_equal(const struct sockaddr_storage* a, const struct sockaddr_storage* b);

bool ct_address_families_match(const ct_local_endpoint_t* local, const ct_remote_endpoint_t* remote);

bool ct_address_scope_match(const ct_local_endpoint_t* local, const ct_remote_endpoint_t* remote);

bool ct_address_is_wildcard(const struct sockaddr_storage* addr);

#endif //UTIL_H
