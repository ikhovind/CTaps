#ifndef CT_REMOTE_ENDPOINT_H
#define CT_REMOTE_ENDPOINT_H
#include <stdint.h>
#include <ctaps.h>

void ct_remote_endpoint_build(ct_remote_endpoint_t* remote_endpoint);

int32_t remote_endpoint_get_service_port(const ct_remote_endpoint_t* remote_endpoint);

const struct sockaddr_storage* remote_endpoint_get_resolved_address(const ct_remote_endpoint_t* remote_endpoint);

#endif
