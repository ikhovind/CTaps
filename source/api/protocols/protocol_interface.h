#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H

#include "message/message.h"
#include "transport_properties/selection_properties/selection_properties.h"

struct Connection;

typedef struct {
  SelectionPreference values[SELECTION_PROPERTY_END];
} ProtocolFeatures;

typedef int (*ReceiveMessageCb)(struct Connection* connection,
                                Message** received_message
                                );

typedef int (*InitDoneCb)(struct Connection* connection);

typedef struct ProtocolImplementation {
  const char* name;
  ProtocolFeatures features;
  int (*init)(struct Connection* connection, InitDoneCb init_done_cb);
  int (*send)(struct Connection*, Message*);
                 // TODO - public callbacks should probably have a void* for context
  int (*receive)(struct Connection*, ReceiveMessageCb receive_cb);
  int (*close)(const struct Connection*);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
