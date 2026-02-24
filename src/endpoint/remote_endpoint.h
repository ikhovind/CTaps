#ifndef CT_REMOTE_ENDPOINT_H
#define CT_REMOTE_ENDPOINT_H
#include <ctaps.h>
#include <stdint.h>
#include "candidate_gathering/candidate_gathering.h"

void ct_remote_endpoint_build(ct_remote_endpoint_t* remote_endpoint);

int32_t remote_endpoint_get_service_port(const ct_remote_endpoint_t* remote_endpoint);

const struct sockaddr_storage* remote_endpoint_get_resolved_address(const ct_remote_endpoint_t* remote_endpoint);

int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint, ct_remote_resolve_call_context_t* context);


#endif
