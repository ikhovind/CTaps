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

int ct_local_endpoint_copy_content(const ct_local_endpoint_t* src, ct_local_endpoint_t* dest);

ct_local_endpoint_t* ct_local_endpoint_deep_copy(const ct_local_endpoint_t* local_endpoint);

ct_local_endpoint_t* ct_local_endpoints_deep_copy(const ct_local_endpoint_t* local_endpoints, size_t num_local_endpoints);

void ct_local_endpoints_free(ct_local_endpoint_t* local_endpoints, size_t num_local_endpoints);

/**
 * @brief Resolve a local endpoint to concrete addresses.
 * @param[in] local_endpoint Endpoint to resolve
 * @param[out] out_count Number of endpoints in output array
 * @return List of resolved local endpoints, NULL on error. Caller is responsible for freeing the returned array and its contents with ct_local_endpoints_free().
 */
ct_local_endpoint_t* ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, size_t* out_count);

bool ct_local_endpoint_resolved_equals(const ct_local_endpoint_t* endpoint1, const ct_local_endpoint_t* endpoint2);

/**
* @brief Set the active local endpoint on a connection by matching a local endpoint object.
*
* @note Invalidates the all_local_endponts_list pointer if the local endpoint is not already in the list
*/
int ct_connection_set_active_local_endpoint(ct_connection_t* connection, const ct_local_endpoint_t* local_endpoint);

#endif
