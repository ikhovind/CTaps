#ifndef CT_REMOTE_ENDPOINT_H
#define CT_REMOTE_ENDPOINT_H
#include <ctaps.h>
#include <stdint.h>
#include "candidate_gathering/candidate_gathering.h"

void ct_remote_endpoint_build(ct_remote_endpoint_t* remote_endpoint);

int32_t remote_endpoint_get_service_port(const ct_remote_endpoint_t* remote_endpoint);

const struct sockaddr_storage*
remote_endpoint_get_resolved_address(const ct_remote_endpoint_t* remote_endpoint);

int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint,
                               ct_remote_resolve_call_context_t* context);

ct_remote_endpoint_t* ct_remote_endpoints_deep_copy(const ct_remote_endpoint_t* remote_endpoints,
                                                    size_t num_remote_endpoints);

int ct_remote_endpoint_copy_content(const ct_remote_endpoint_t* src, ct_remote_endpoint_t* dest);

void ct_remote_endpoints_free(ct_remote_endpoint_t* remote_endpoints, size_t num_remote_endpoints);

// Compare only the resolved sockaddr, not the strings etc.
bool ct_remote_endpoint_resolved_equals(const ct_remote_endpoint_t* endpoint1,
                                        const ct_remote_endpoint_t* endpoint2);

#endif
