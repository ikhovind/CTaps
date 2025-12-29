#ifndef CT_PRECONNECTION_H
#define CT_PRECONNECTION_H
#include <ctaps.h>

const ct_local_endpoint_t* preconnection_get_local_endpoint(const ct_preconnection_t* preconnection);

ct_remote_endpoint_t* const * preconnection_get_remote_endpoints(const ct_preconnection_t* preconnection, size_t* out_count);

const ct_transport_properties_t* preconnection_get_transport_properties(const ct_preconnection_t* preconnection);


#endif
