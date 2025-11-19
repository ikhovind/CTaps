#ifndef UTIL_H
#define UTIL_H
#define MAX_FOUND_INTERFACE_ADDRS 64

#include <uv.h>
#include "ctaps.h"

void get_interface_addresses(const char *interface_name, int *num_found_addresses, struct sockaddr_storage *output_interface_addrs);

int perform_dns_lookup(const char* hostname, const char* service, ct_remote_endpoint_t** out_list, size_t* out_count, uv_getaddrinfo_cb getaddrinfo_cb);

#endif //UTIL_H
