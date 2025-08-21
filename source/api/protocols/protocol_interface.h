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
  int (*init)(struct Connection* connection);
  int (*send)(struct Connection*, Message*);
  Message* (*receive)(struct Connection*);
  void (*close)(struct Connection*);
} ProtocolImplementation;

#endif  // PROTOCOL_INTERFACE_H
