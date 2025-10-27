#ifndef LISTENER_H
#define LISTENER_H
#include <endpoints/local/local_endpoint.h>
#include <stddef.h>
#include <transport_properties/transport_properties.h>

#include "listener_callbacks.h"

// forward declaration of socket manager
struct SocketManager;



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
  struct SocketManager* socket_manager;
} Listener;

void listener_close(const Listener* listener);

LocalEndpoint listener_get_local_endpoint(const Listener* listener);
#endif //LISTENER_H
