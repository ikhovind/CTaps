#ifndef LISTENER_H
#define LISTENER_H
#include <stddef.h>
#include <endpoints/local/local_endpoint.h>
#include <transport_properties/transport_properties.h>

#include "socket_manager/socket_manager.h"


/*
 * TODO - needed features for Listener:
 * - Support for optional remote endpoint, to filter incoming connections
 * - New connection limit
 * - Figure out what to do with buffer pool
 * Missing Events:
 * - Establishment error
 * - Stopped event
 */
typedef int (*ConnectionReceivedCb)(struct Listener* source, struct Connection* new_connection);

typedef struct Listener {
  TransportProperties transport_properties;
  LocalEndpoint local_endpoint;
  size_t num_local_endpoints;
  ConnectionReceivedCb connection_received_cb;
  SocketManager* socket_manager;
  void* user_data;
} Listener;

void listener_close(Listener* listener);

#endif //LISTENER_H
