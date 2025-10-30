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
typedef struct {
  TransportProperties transport_properties;
  const SecurityParameters* security_parameters;
  LocalEndpoint local;
  size_t num_local_endpoints;
  RemoteEndpoint* remote_endpoints;
  size_t num_remote_endpoints;
} Preconnection;

// TODO - change other init functions to build, to avoid confusing with initiate
int preconnection_build(Preconnection* preconnection,
                         const TransportProperties transport_properties,
                         const RemoteEndpoint* remote_endpoints,
                         size_t num_remote_endpoints,
                         const SecurityParameters* security_parameters
                         );

int preconnection_build_with_local(Preconnection* preconnection,
                                    TransportProperties transport_properties,
                                    RemoteEndpoint remote_endpoints[],
                                    size_t num_remote_endpoints,
                                    const SecurityParameters* security_parameters,
                                    LocalEndpoint local_endpoint);

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                           ConnectionCallbacks connection_callbacks);

int preconnection_listen(Preconnection* preconnection, Listener* listener, ListenerCallbacks listener_callback);

void preconnection_free(Preconnection* preconnection);

void preconnection_initiate_with_timeout(Preconnection* preconnection,
                                         Connection* connection,
                                         int timeout_ms);

#endif  // PRECONNECTION_H
