//
// Created by ikhovind on 12.08.25.
//

#ifndef PRECONNECTION_H
#define PRECONNECTION_H

#include "connections/connection/connection.h"
#include "connections/listener/listener.h"
#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "security_parameters/security_parameters.h"

/*
 * TODO:
 *   - Multiple local endpoints
 *   - Actual use multiple remote
 *   - Security parameters
 *   - Rendezvous
 *
 */
typedef struct ct_preconnection_s {
  ct_transport_properties_t transport_properties;
  const ct_security_parameters_t* security_parameters;
  ct_local_endpoint_t local;
  size_t num_local_endpoints;
  ct_remote_endpoint_t* remote_endpoints;
  size_t num_remote_endpoints;
} ct_preconnection_t;

// TODO - change other init functions to build, to avoid confusing with initiate
int ct_preconnection_build(ct_preconnection_t* preconnection,
                         const ct_transport_properties_t transport_properties,
                         const ct_remote_endpoint_t* remote_endpoints,
                         size_t num_remote_endpoints,
                         const ct_security_parameters_t* security_parameters
                         );

int ct_preconnection_build_with_local(ct_preconnection_t* preconnection,
                                    ct_transport_properties_t transport_properties,
                                    ct_remote_endpoint_t remote_endpoints[],
                                    size_t num_remote_endpoints,
                                    const ct_security_parameters_t* security_parameters,
                                    ct_local_endpoint_t local_endpoint);

int ct_preconnection_initiate(ct_preconnection_t* preconnection, ct_connection_t* connection,
                           ct_connection_callbacks_t connection_callbacks);

int ct_preconnection_listen(ct_preconnection_t* preconnection, ct_listener_t* listener, ct_listener_callbacks_t listener_callback);

void ct_preconnection_free(ct_preconnection_t* preconnection);

void ct_preconnection_initiate_with_timeout(ct_preconnection_t* preconnection,
                                         ct_connection_t* connection,
                                         int timeout_ms);

void ct_preconnection_build_user_connection(ct_connection_t* connection, const ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks);


#endif  // PRECONNECTION_H
