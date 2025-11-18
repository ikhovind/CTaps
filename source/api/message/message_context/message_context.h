//
// Created by ikhovind on 10.10.25.
//

#ifndef MESSAGE_CONTEXT_H
#define MESSAGE_CONTEXT_H
#include "transport_properties/message_properties/message_properties.h"
#include "endpoints/remote/remote_endpoint.h"
#include "endpoints/local/local_endpoint.h"

typedef struct ct_message_context_t {
  ct_message_properties_t message_properties;
  ct_local_endpoint_t* local_endpoint;
  ct_remote_endpoint_t* remote_endpoint;
} ct_message_context_t;

#endif //MESSAGE_CONTEXT_H
