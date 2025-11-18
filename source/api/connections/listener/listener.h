#ifndef LISTENER_H
#define LISTENER_H
#include <endpoints/local/local_endpoint.h>
#include <stddef.h>
#include <transport_properties/transport_properties.h>
#include "security_parameters/security_parameters.h"

#include "listener_callbacks.h"

// forward declaration of socket manager
struct ct_socket_manager_t;



/*
 * TODO - needed features for ct_listener_t:
 * - Support for optional remote endpoint, to filter incoming connections
 * - New connection limit
 * - Figure out what to do with buffer pool
 */
typedef struct ct_listener_t {
  ct_transport_properties_t transport_properties;
  ct_local_endpoint_t local_endpoint;
  size_t num_local_endpoints;
  ct_listener_callbacks_t listener_callbacks;
  const ct_security_parameters_t* security_parameters;
  struct ct_socket_manager_t* socket_manager;
} ct_listener_t;

void ct_listener_close(const ct_listener_t* listener);

ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener);
#endif //LISTENER_H
