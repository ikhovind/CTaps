#ifndef LISTENER_H
#define LISTENER_H
#include <stddef.h>
#include <endpoints/local/local_endpoint.h>
#include <transport_properties/transport_properties.h>


typedef int (*ConnectionReceived)(struct Connection* connection, void* user_data);

typedef struct Listener {
  TransportProperties transport_properties;
  LocalEndpoint local;
  size_t num_local_endpoints;
} Listener;

#endif //LISTENER_H
