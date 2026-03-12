#ifndef CT_LISTENER_H
#define CT_LISTENER_H
#include "ctaps.h"
#include "ctaps_internal.h"

ct_listener_t* ct_listener_new(
  const ct_transport_properties_t* transport_properties,
  const ct_local_endpoint_t* local_endpoint,
  const ct_listener_callbacks_t* listener_callbacks,
  const ct_connection_callbacks_t* connection_callbacks,
  const ct_security_parameters_t* security_parameters,
  const ct_protocol_impl_t* protocol_impl
);

/**
 * @brief Get the local endpoint a listener is bound to.
 * @param[in] listener The listener
 * @return Local endpoint structure (copy)
 */
const ct_local_endpoint_t* ct_listener_get_local_endpoint(const ct_listener_t* listener);

void ct_listener_mark_as_closed(ct_listener_t* listener);

#endif
