#ifndef UTIL_H
#define UTIL_H
#include "candidate_gathering/candidate_gathering.h"
#define MAX_FOUND_INTERFACE_ADDRS 64

#include <uv.h>

void get_interface_addresses(const char* interface_name, int* num_found_addresses,
                             struct sockaddr_storage* output_interface_addrs);

int ct_perform_dns_lookup(const char* hostname, const char* service,
                       ct_remote_resolve_call_context_t* context);

bool ct_sockaddr_equal(const struct sockaddr_storage* a, const struct sockaddr_storage* b);

#endif //UTIL_H
