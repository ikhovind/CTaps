#ifndef UTIL_H
#define UTIL_H
#define MAX_FOUND_INTERFACE_ADDRS 64

#include <endpoints/local/local_endpoint.h>

void get_interface_addresses(LocalEndpoint *local_endpoint, int *num_found_addresses, struct sockaddr_storage *output_interface_addrs);

int get_service_port(LocalEndpoint* local_endpoint);

#endif //UTIL_H
