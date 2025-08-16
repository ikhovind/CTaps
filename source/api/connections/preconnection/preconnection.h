//
// Created by ikhovind on 12.08.25.
//

#ifndef PRECONNECTION_H
#define PRECONNECTION_H

#include "transport_properties/transport_properties.h"
#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "connections/connection/connection.h"

// TODO - security parameters
// TODO - listen & rendezvous
typedef struct {
    TransportProperties transport_properties;
    RemoteEndpoint* remote;
    unsigned int num_remote_endpoints;
    LocalEndpoint* local;
    unsigned int num_local_endpoints;
} Preconnection;

// TODO - change other init functions to build, to avoid confusing with initiate
void preconnection_build(Preconnection *preconnection, TransportProperties transport_properties);

void preconnection_initiate(Preconnection* preconnection, Connection* connection);

void preconnection_initiate_with_timeout(Preconnection* preconnection, Connection* connection, int timeout_ms);

#endif //PRECONNECTION_H
