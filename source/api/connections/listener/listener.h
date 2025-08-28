#ifndef LISTENER_H
#define LISTENER_H
#include <stddef.h>
#include <endpoints/local/local_endpoint.h>
#include <transport_properties/transport_properties.h>

#include "socket_manager/socket_manager.h"


typedef int (*ConnectionReceivedCb)(struct Connection* connection, void* user_data);

typedef struct Listener {
  TransportProperties transport_properties;
  LocalEndpoint local_endpoint;
  size_t num_local_endpoints;
  ConnectionReceivedCb connection_received_cb;
  ProtocolImplementation protocol;
  SocketManager* socket_manager;
} Listener;

#endif //LISTENER_H
