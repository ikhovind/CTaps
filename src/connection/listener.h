#ifndef CT_LISTENER_H
#define CT_LISTENER_H
#include "ctaps.h"

ct_listener_t* ct_listener_new(void);

/**
 * @brief Get the local endpoint a listener is bound to.
 * @param[in] listener The listener
 * @return Local endpoint structure (copy)
 */
ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener);

#endif
