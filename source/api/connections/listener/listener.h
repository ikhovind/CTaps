#ifndef LISTENER_H
#define LISTENER_H
#include <stddef.h>
#include <endpoints/local/local_endpoint.h>
#include <transport_properties/transport_properties.h>

#include "listener_callbacks.h"
#include "socket_manager/socket_manager.h"


/*
 * TODO - needed features for Listener:
 * - Support for optional remote endpoint, to filter incoming connections
 * - New connection limit
 * - Figure out what to do with buffer pool
 */
typedef struct Listener {
  TransportProperties transport_properties;
  LocalEndpoint local_endpoint;
  size_t num_local_endpoints;
  ListenerCallbacks listener_callbacks;
  SocketManager* socket_manager;
} Listener;

void listener_close(const Listener* listener);

#endif //LISTENER_H
