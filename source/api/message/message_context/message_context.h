//
// Created by ikhovind on 10.10.25.
//

#ifndef MESSAGE_CONTEXT_H
#define MESSAGE_CONTEXT_H
#include "transport_properties/message_properties/message_properties.h"
#include "endpoints/remote/remote_endpoint.h"
#include "endpoints/local/local_endpoint.h"

typedef struct MessageContext {
  MessageProperties message_properties;
  LocalEndpoint* local_endpoint;
  RemoteEndpoint* remote_endpoint;
} MessageContext;

#endif //MESSAGE_CONTEXT_H
