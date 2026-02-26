#ifndef CT_LOCAL_ENDPOINT_H
#define CT_LOCAL_ENDPOINT_H
#include <stdint.h>
#include <ctaps.h>


/**
 * @brief Free string fields in a local endpoint without freeing the structure.
 * @param[in] local_endpoint Endpoint whose strings to free
 */
void ct_local_endpoint_free_strings(ct_local_endpoint_t* local_endpoint);

void ct_local_endpoint_build(ct_local_endpoint_t* local_endpoint);

int32_t local_endpoint_get_service_port(const ct_local_endpoint_t* local_endpoint);

const struct sockaddr_storage* local_endpoint_get_resolved_address(const ct_local_endpoint_t* local_endpoint);

char* local_endpoint_get_interface_name(const ct_local_endpoint_t* local_endpoint);

uint16_t local_endpoint_get_resolved_port(const ct_local_endpoint_t* local_endpoint);

sa_family_t local_endpoint_get_address_family(const ct_local_endpoint_t* local_endpoint);

ct_local_endpoint_t* ct_local_endpoint_deep_copy(const ct_local_endpoint_t* local_endpoint);

#endif
