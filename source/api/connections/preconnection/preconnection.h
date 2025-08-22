//
// Created by ikhovind on 12.08.25.
//

#ifndef PRECONNECTION_H
#define PRECONNECTION_H

#include "connections/connection/connection.h"
#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"

// TODO - security parameters
// TODO - listen & rendezvous
typedef struct {
  TransportProperties transport_properties;
  RemoteEndpoint remote;
  unsigned int num_remote_endpoints;
  LocalEndpoint local;
  unsigned int num_local_endpoints;
} Preconnection;

// TODO - change other init functions to build, to avoid confusing with initiate
void preconnection_build(Preconnection* preconnection,
                         TransportProperties transport_properties,
                         RemoteEndpoint remote_endpoint);

void preconnection_build_with_local(Preconnection* preconnection,
                                    TransportProperties transport_properties,
                                    RemoteEndpoint remote_endpoint,
                                    LocalEndpoint local_endpoint);

int preconnection_initiate(Preconnection* preconnection, Connection* connection,
                            InitDoneCb init_done_cb);

void preconnection_initiate_with_timeout(Preconnection* preconnection,
                                         Connection* connection,
                                         int timeout_ms);

#endif  // PRECONNECTION_H
