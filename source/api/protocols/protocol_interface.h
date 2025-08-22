#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include "message/message.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct Connection;

typedef struct {
  SelectionPreference values[SELECTION_PROPERTY_END];
} ProtocolFeatures;

typedef struct ProtocolImplementation {
  const char* name;
  ProtocolFeatures features;
  int (*init)(struct Connection* connection,
              int (*init_done_cb)(struct Connection* connection));
  int (*send)(struct Connection*, Message*);
  int (*receive)(struct Connection*,
                 // TODO - public callbacks should probably have a void* for context
                 int (*receive_msg_cb)(struct Connection* connection,
                                       Message** received_message));
  int (*close)(const struct Connection*);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
