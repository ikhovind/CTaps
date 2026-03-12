#ifndef CT_PRECONNECTION_H
#define CT_PRECONNECTION_H
#include <ctaps.h>

const ct_local_endpoint_t*
ct_preconnection_get_local_endpoints(const ct_preconnection_t* preconnection, size_t* out_count);

ct_remote_endpoint_t* const*
ct_preconnection_get_remote_endpoints(const ct_preconnection_t* preconnection, size_t* out_count);

const ct_transport_properties_t*
ct_preconnection_get_transport_properties(const ct_preconnection_t* preconnection);

const ct_security_parameters_t*
ct_preconnection_get_security_parameters(const ct_preconnection_t* preconnection);

#endif
